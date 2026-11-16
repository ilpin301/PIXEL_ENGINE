// Audio mixer tests (S5.5): exact PCM output, 16.16 resampling, gain/master/saturation, voice stealing, command queue,
// stale handles, pattern timing and pitch, pattern validation, fill-size independence, determinism. Expectations are
// computed in the test from plain integer arithmetic (or floating point for the pitch table), not from the mixer.
// Output is read through the null HAL backend (pge_host_audio_pump) or by calling Mixer::fill directly.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "host_audio.h"
#include "pge/audio.h"
#include "pge/config.h"
#include "pge/hal/audio.h"

#define CHECK(c)                                                          \
  do {                                                                    \
    if (!(c)) {                                                           \
      std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
      return 1;                                                           \
    }                                                                     \
  } while (0)

using namespace pge;
typedef std::vector<int16_t> Samples;

static_assert(PGE_AUDIO_CHANNELS == 8, "the host profile has 8 channels: 4 music + 4 effect");
static const uint32_t kQ = PGE_AUDIO_QUEUE;

static Mixer g_a, g_b, g_fresh;  // g_fresh is never initialised

// Renders n frames (one fill call) and returns the left channel; the right channel must be identical.
static Samples render(Mixer& m, uint32_t n) {
  std::vector<int16_t> st(2 * (size_t)n + 2, 0x5555);
  m.fill(st.data(), n);
  Samples l(n);
  for (uint32_t i = 0; i < n; i++) {
    if (st[2 * i] != st[2 * i + 1]) { std::printf("L != R at frame %u\n", i); std::exit(1); }
    l[i] = st[2 * i];
  }
  if (st[2 * (size_t)n] != 0x5555 || st[2 * (size_t)n + 1] != 0x5555) { std::printf("fill wrote past the end\n"); std::exit(1); }
  return l;
}

static Sound snd16(const Samples& v, uint32_t rate) { return Sound{v.data(), (uint32_t)v.size(), (uint16_t)rate, Pcm16, 0}; }
static Sound snd8(const std::vector<int8_t>& v, uint32_t rate) { return Sound{v.data(), (uint32_t)v.size(), (uint16_t)rate, Pcm8, 0}; }
static Samples constant(size_t n, int16_t v) { return Samples(n, v); }
static bool allEqual(const Samples& s, size_t from, size_t to, int16_t v) {
  for (size_t i = from; i < to; i++) if (s[i] != v) return false;
  return true;
}

// Floor of x / 256 (what an arithmetic >> 8 must give for negative values too).
static int32_t fdiv256(int32_t x) { return x >= 0 ? x / 256 : -((-x + 255) / 256); }
static int16_t clamp16(int32_t x) { return (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x); }

static Samples ramp(size_t n, int mul, int off) {
  Samples s(n);
  for (size_t i = 0; i < n; i++) s[i] = (int16_t)((int)i * mul + off);
  return s;
}

// ---------------------------------------------------------------- init / HAL
static int testInit() {
  int16_t buf[8];
  CHECK(pge_host_audio_pump(buf, 1) == -1);  // nothing registered yet
  CHECK(g_a.init(7999) == InvalidArg);
  CHECK(g_a.init(48001) == InvalidArg);
  CHECK(g_a.init(8000) == Ok);
  CHECK(g_a.init(48000) == Ok);
  CHECK(g_a.init(22050) == Ok);
  CHECK(g_a.start() == Ok);
  CHECK(g_a.stop() == Ok);
  CHECK(pge_hal_audio_init(0, nullptr) == -1);

  std::memset(buf, 0x55, sizeof buf);
  CHECK(pge_host_audio_pump(buf, 4) == 0);
  for (int i = 0; i < 8; i++) CHECK(buf[i] == 0);  // idle mixer: silence, written through the HAL callback
  const Samples one = constant(4, 777);
  g_a.play(snd16(one, 22050));
  CHECK(pge_host_audio_pump(buf, 4) == 0);
  for (int i = 0; i < 8; i++) CHECK(buf[i] == 777);

  CHECK(g_fresh.playMusic(Pattern{}, false) == NotInitialized);
  CHECK(!g_fresh.play(snd16(one, 22050)).valid());
  CHECK(allEqual(render(g_fresh, 16), 0, 16, 0));
  return 0;
}

// ---------------------------------------------------------------- plain playback
static int testPcmExact() {
  CHECK(g_a.init(22050) == Ok);
  const Samples s = {0, 1, -1, 32767, -32768, 12345, -12345, 100};
  const Voice v = g_a.play(snd16(s, 22050));
  CHECK(v.valid());
  CHECK(!g_a.isPlaying(v));  // not drained yet
  Samples o = render(g_a, 3);
  CHECK(g_a.isPlaying(v));
  for (int i = 0; i < 3; i++) CHECK(o[i] == s[i]);
  o = render(g_a, 5);
  for (int i = 0; i < 5; i++) CHECK(o[i] == s[3 + i]);
  CHECK(!g_a.isPlaying(v));
  CHECK(allEqual(render(g_a, 20), 0, 20, 0));

  const std::vector<int8_t> p8 = {0, 1, -1, 127, -128, 64, -64};
  g_a.play(snd8(p8, 22050));
  o = render(g_a, 9);
  for (size_t i = 0; i < p8.size(); i++) CHECK(o[i] == p8[i] * 256);
  CHECK(o[7] == 0 && o[8] == 0);
  return 0;
}

static int testResample() {
  const Samples r = ramp(3000, 7, -9000);
  struct C { uint32_t mix, rate; } cases[] = {{22050, 11025}, {22050, 44100}, {22050, 33075}, {32000, 22050}, {8000, 22050}, {48000, 8000}};
  for (const C& c : cases) {
    CHECK(g_a.init(c.mix) == Ok);
    g_a.play(snd16(r, c.rate));
    const Samples o = render(g_a, 1000);
    // 16.16 rule, written out: step floors the ratio, the fraction carries into the integer position.
    const uint32_t step = (uint32_t)(((uint64_t)c.rate << 16) / c.mix);
    uint32_t pos = 0, frac = 0;
    for (uint32_t i = 0; i < 1000; i++) {
      CHECK(o[i] == r[pos]);
      CHECK(pos == (uint32_t)(((uint64_t)i * step) >> 16));  // same thing in closed form
      frac += step & 0xFFFF;
      pos += (step >> 16) + (frac >> 16);
      frac &= 0xFFFF;
    }
  }
  CHECK(g_a.init(22050) == Ok);  // spelled-out ratios
  g_a.play(snd16(r, 11025));
  Samples o = render(g_a, 20);
  for (int i = 0; i < 20; i++) CHECK(o[i] == r[i / 2]);
  g_a.init(22050);
  g_a.play(snd16(r, 44100));
  o = render(g_a, 20);
  for (int i = 0; i < 20; i++) CHECK(o[i] == r[2 * i]);
  g_a.init(22050);
  g_a.play(snd16(r, 33075));
  o = render(g_a, 20);
  for (int i = 0; i < 20; i++) CHECK(o[i] == r[(3 * i) / 2]);
  return 0;
}

static int testGain() {
  const Samples s = {1000, -1000, 32767, -32768, 255, -255, 1, -1, 0, 12345, -12345, 200};
  CHECK(g_a.init(22050) == Ok);
  g_a.play(snd16(s, 22050), 100);
  g_a.setMaster(128);
  Samples o = render(g_a, 12);
  for (size_t i = 0; i < s.size(); i++) CHECK(o[i] == clamp16(fdiv256(fdiv256(s[i] * 101) * 129)));
  CHECK(o[1] == fdiv256(fdiv256(-1000 * 101) * 129));
  CHECK(o[6] == 0 && o[7] == -1);  // floor, not truncation: -1 stays -1

  const std::vector<int8_t> p8 = {100, -100, 127, -128};
  g_a.init(22050);
  g_a.play(snd8(p8, 22050), 0);  // volume 0 is (s * 1) >> 8, not silence
  o = render(g_a, 4);
  for (int i = 0; i < 4; i++) CHECK(o[i] == fdiv256(p8[i] * 256 * 1));

  // volume change reaches the running voice at the next fill
  g_a.init(22050);
  const Samples c = constant(100, 20000);
  const Voice v = g_a.play(snd16(c, 22050), 255, true);
  o = render(g_a, 4);
  CHECK(allEqual(o, 0, 4, 20000));
  g_a.setVolume(v, 63);
  o = render(g_a, 4);
  CHECK(allEqual(o, 0, 4, (int16_t)fdiv256(20000 * 64)));
  g_a.setMaster(0);
  o = render(g_a, 2);
  CHECK(allEqual(o, 0, 2, (int16_t)fdiv256(fdiv256(20000 * 64) * 1)));
  return 0;
}

static int testSumAndSaturation() {
  CHECK(g_a.init(22050) == Ok);
  const Samples a = constant(10, 1000), b = constant(10, -300), big = constant(10, 30000), nbig = constant(10, -30000);
  g_a.play(snd16(a, 22050));
  g_a.play(snd16(b, 22050));
  CHECK(allEqual(render(g_a, 5), 0, 5, 700));
  g_a.init(22050);
  g_a.play(snd16(big, 22050));
  g_a.play(snd16(big, 22050));
  CHECK(allEqual(render(g_a, 5), 0, 5, 32767));
  g_a.init(22050);
  g_a.play(snd16(nbig, 22050));
  g_a.play(snd16(nbig, 22050));
  CHECK(allEqual(render(g_a, 5), 0, 5, -32768));
  g_a.init(22050);
  const Samples ext = constant(10, 32767), next = constant(10, -32768);
  for (int i = 0; i < 4; i++) g_a.play(snd16(ext, 22050));
  CHECK(allEqual(render(g_a, 5), 0, 5, 32767));  // 4 * 32767 must not wrap
  g_a.init(22050);
  for (int i = 0; i < 4; i++) g_a.play(snd16(next, 22050));
  CHECK(allEqual(render(g_a, 5), 0, 5, -32768));
  return 0;
}

static int testEndAndLoop() {
  CHECK(g_a.init(22050) == Ok);
  const Samples s = {10, 20, 30, 40, 50};
  const Voice one = g_a.play(snd16(s, 22050));
  Samples o = render(g_a, 12);
  CHECK(o[4] == 50 && allEqual(o, 5, 12, 0));
  CHECK(!g_a.isPlaying(one));

  const Voice lp = g_a.play(snd16(s, 22050), 255, true);
  o = render(g_a, 23);
  for (int i = 0; i < 23; i++) CHECK(o[i] == s[i % 5]);
  CHECK(g_a.isPlaying(lp));
  g_a.stop(lp);
  CHECK(allEqual(render(g_a, 5), 0, 5, 0));
  CHECK(!g_a.isPlaying(lp));

  // a loop that wraps in the middle of a block continues with the right fractional phase
  g_a.init(22050);
  const Samples r = ramp(7, 100, 0);
  g_a.play(snd16(r, 33075), 255, true);
  o = render(g_a, 40);
  uint32_t pos = 0, frac = 0;
  const uint32_t step = (33075u << 16) / 22050;
  for (int i = 0; i < 40; i++) {
    CHECK(o[i] == r[pos]);
    frac += step & 0xFFFF;
    pos = (pos + (step >> 16) + (frac >> 16)) % 7;
    frac &= 0xFFFF;
  }
  return 0;
}

// ---------------------------------------------------------------- voices
static int testStealing() {
  CHECK(g_a.init(22050) == Ok);
  const int vals[9] = {10, 20, 40, 80, 160, 320, 640, 1280, 2560};
  static Samples snds[9];
  for (int i = 0; i < 9; i++) snds[i] = constant(16, (int16_t)vals[i]);
  const uint8_t prio[9] = {1, 0, 0, 2, 1, 0, 1, 1, 3};  // A..I
  Voice v[9];
  int alive[9] = {};
  auto play = [&](int i) { v[i] = g_a.play(snd16(snds[i], 22050), 255, true, prio[i]); };
  auto sum = [&]() {
    int s = 0;
    for (int i = 0; i < 9; i++) s += alive[i] ? vals[i] : 0;
    return s;
  };
  auto step = [&](int i, int kills, int stolen, int rejected, bool live) -> int {
    play(i);
    alive[i] = live;
    if (kills >= 0) alive[kills] = 0;
    const Samples o = render(g_a, 1);
    CHECK(o[0] == sum());
    CHECK(g_a.stats().stolen == (uint32_t)stolen && g_a.stats().rejected == (uint32_t)rejected);
    for (int k = 0; k <= i; k++) CHECK(g_a.isPlaying(v[k]) == (alive[k] != 0));
    return 0;
  };
  for (int i = 0; i < 4; i++) { play(i); alive[i] = 1; }  // A p1, B p0, C p0, D p2
  CHECK(render(g_a, 1)[0] == sum());
  CHECK(step(4, 1, 1, 0, true) == 0);   // E p1 steals B: lowest priority, oldest
  CHECK(step(5, -1, 1, 1, false) == 0); // F p0 equals the lowest (C): rejected
  CHECK(step(6, 2, 2, 1, true) == 0);   // G p1 steals C
  CHECK(step(7, -1, 2, 2, false) == 0); // H p1 equals the lowest (A, E, G): rejected
  CHECK(step(8, 0, 3, 2, true) == 0);   // I p3 steals A, the oldest of the p1 voices
  CHECK(g_a.stats().dropped_cmds == 0);
  return 0;
}

static int testQueue() {
  CHECK(g_a.init(22050) == Ok);
  const Samples c = constant(8, 100);
  for (uint32_t i = 0; i < kQ; i++) CHECK(g_a.play(snd16(c, 22050), 255, true).valid());
  CHECK(g_a.stats().dropped_cmds == 0);
  CHECK(!g_a.play(snd16(c, 22050), 255, true).valid());
  CHECK(g_a.stats().dropped_cmds == 1);
  g_a.setMaster(1);
  g_a.stop(Voice{1});
  g_a.stopMusic();
  CHECK(g_a.stats().dropped_cmds == 4);
  const PatternEvent ev = {0, 0, 0, 60, 255};
  const Samples one = constant(4, 1);
  const Sound inst = snd16(one, 22050);
  const Pattern pat = {&ev, 1, 4, &inst, 1, 120, 4};
  CHECK(g_a.playMusic(pat, false) == Busy);
  CHECK(g_a.stats().dropped_cmds == 5);
  const Samples o = render(g_a, 1);  // the dropped setMaster(1) was lost: 4 voices at full volume
  CHECK(o[0] == 400);
  CHECK(g_a.stats().rejected == kQ - 4);
  CHECK(g_a.play(snd16(c, 22050), 255, true, 9).valid());  // drained: accepts again
  CHECK(g_a.stats().dropped_cmds == 5);
  return 0;
}

static int testStaleHandle() {
  CHECK(g_a.init(22050) == Ok);
  const Samples shortS = constant(3, 111), longS = constant(50, 222);
  const Voice a = g_a.play(snd16(shortS, 22050));
  render(g_a, 10);  // a ended and freed its slot
  const Voice b = g_a.play(snd16(longS, 22050), 255, true);
  CHECK(a.id != b.id);
  CHECK(render(g_a, 1)[0] == 222);
  g_a.stop(a);
  g_a.setVolume(a, 0);
  CHECK(render(g_a, 3)[2] == 222);  // the stale handle touches nothing
  CHECK(g_a.isPlaying(b) && !g_a.isPlaying(a));
  g_a.stop(b);
  CHECK(render(g_a, 2)[0] == 0);

  CHECK(g_a.init(22050) == Ok);  // ticket 0 is never handed out, also across the u16 wrap
  for (int i = 0; i < 70000; i++) {
    const Voice v = g_a.play(snd16(shortS, 22050));
    if (!v.valid() || v.id == 0) { std::printf("bad ticket at play %d\n", i); return 1; }
    g_a.fill(nullptr, 0);
  }
  CHECK(g_a.stats().dropped_cmds == 0);
  return 0;
}

// ---------------------------------------------------------------- music
// 4-row pattern, 24000 Hz, 120 bpm, 4 rows/beat: 3000 frames per row.
// ch0 starts at row 0 (frame 0), ch1 at row 2 (6000), both stop at row 3 (9000), the pattern ends at row 4 (12000).
static int testMusicTiming() {
  const Samples c = constant(50, 1000);
  const Sound inst[1] = {Sound{c.data(), 50, 24000, Pcm16, 1}};
  const PatternEvent ev[] = {{0, 0, 0, 60, 255}, {2, 1, 0, 60, 255}, {3, 0, 0, 0, 0}, {3, 1, 0, 0, 0}};
  const Pattern pat = {ev, 4, 4, inst, 1, 120, 4};
  auto expect = [](uint32_t t) -> int16_t { t %= 12000; return t < 6000 ? 1000 : t < 9000 ? 2000 : 0; };

  CHECK(g_a.init(24000) == Ok);
  CHECK(g_a.playMusic(pat, true) == Ok);
  CHECK(!g_a.musicPlaying());
  Samples o = render(g_a, 30000);
  CHECK(g_a.musicPlaying());
  for (uint32_t t = 0; t < 30000; t++) CHECK(o[t] == expect(t));

  CHECK(g_a.init(24000) == Ok);  // not looping
  CHECK(g_a.playMusic(pat, false) == Ok);
  o = render(g_a, 11999);
  CHECK(g_a.musicPlaying());
  o.push_back(render(g_a, 1)[0]);
  CHECK(!g_a.musicPlaying());
  for (uint32_t t = 0; t < 12000; t++) CHECK(o[t] == expect(t));
  CHECK(allEqual(render(g_a, 100), 0, 100, 0));

  g_a.playMusic(pat, true);
  render(g_a, 7000);
  g_a.stopMusic();
  CHECK(allEqual(render(g_a, 50), 0, 50, 0));
  CHECK(!g_a.musicPlaying());

  // Not whole frames: 22050 Hz, 97 bpm, 4 rows/beat = 3409.79 frames per row. Row r starts at ceil(r * 1323000 / 388).
  const Sound i2[1] = {Sound{c.data(), 50, 22050, Pcm16, 1}};
  const PatternEvent e2[] = {{0, 0, 0, 60, 255}, {1, 1, 0, 60, 255}, {2, 2, 0, 60, 255}, {3, 3, 0, 60, 255}};
  const Pattern p2 = {e2, 4, 8, i2, 1, 97, 4};
  CHECK(g_a.init(22050) == Ok);
  CHECK(g_a.playMusic(p2, false) == Ok);
  o = render(g_a, 12000);
  uint32_t start[4];
  for (uint32_t r = 0; r < 4; r++) start[r] = (r * 1323000u + 387) / 388;
  CHECK(start[1] == 3410 && start[2] == 6820 && start[3] == 10230);
  for (uint32_t t = 0; t < 12000; t++) {
    int n = 0;
    for (int r = 0; r < 4; r++) n += t >= start[r];
    CHECK(o[t] == 1000 * n);
  }
  return 0;
}

static int testMusicPitch() {
  const Samples r = ramp(3000, 10, -15000);
  const Sound inst[1] = {Sound{r.data(), 3000, 22050, Pcm16, 1}};
  uint32_t semi[12];
  for (int i = 0; i < 12; i++) {
    semi[i] = (uint32_t)std::lround(65536.0 * std::pow(2.0, i / 12.0));
    CHECK(detail::kSemi[i] == semi[i]);
  }
  for (int note = 24; note <= 127; note++) {
    const PatternEvent ev[1] = {{0, 2, 0, (uint8_t)note, 255}};
    const Pattern pat = {ev, 1, 2, inst, 1, 120, 4};
    CHECK(g_a.init(22050) == Ok);
    CHECK(g_a.playMusic(pat, false) == Ok);
    const Samples o = render(g_a, 1000);
    const int d = note - 60, oct = (int)std::floor(d / 12.0), idx = d - 12 * oct;
    uint64_t step = oct >= 0 ? (uint64_t)semi[idx] << oct : (uint64_t)semi[idx] >> -oct;
    if (step > (16u << 16)) step = 16u << 16;
    for (uint64_t i = 0; i < 1000; i++) CHECK(o[i] == r[((i * step) >> 16) % 3000]);
  }
  // spelled out: +12 takes every second sample, -12 repeats each sample
  for (int dir = 0; dir < 2; dir++) {
    const PatternEvent ev[1] = {{0, 0, 0, (uint8_t)(dir ? 48 : 72), 255}};
    const Pattern pat = {ev, 1, 2, inst, 1, 120, 4};
    g_a.init(22050);
    g_a.playMusic(pat, false);
    const Samples o = render(g_a, 100);
    for (int i = 0; i < 100; i++) CHECK(o[i] == r[dir ? i / 2 : 2 * i]);
  }
  return 0;
}

static int testMusicAndSfx() {
  const Samples m = constant(20, 1000), s = constant(20, 100);
  const Sound inst[1] = {Sound{m.data(), 20, 22050, Pcm16, 1}};
  const PatternEvent ev[] = {{0, 0, 0, 60, 255}, {0, 3, 0, 60, 255}};
  const Pattern pat = {ev, 2, 4, inst, 1, 60, 1};
  CHECK(g_a.init(22050) == Ok);
  CHECK(g_a.playMusic(pat, true) == Ok);
  for (int i = 0; i < 20; i++) g_a.play(snd16(s, 22050), 255, true);  // floods the 4 effect slots
  Samples o = render(g_a, 10);
  CHECK(allEqual(o, 0, 10, 2000 + 400));
  CHECK(g_a.stats().rejected == 16 && g_a.stats().stolen == 0);
  const Voice hi = g_a.play(snd16(s, 22050), 255, true, 5);
  CHECK(render(g_a, 1)[0] == 2400 && g_a.isPlaying(hi) && g_a.stats().stolen == 1);
  CHECK(g_a.musicPlaying());
  return 0;
}

static int testPatternValidation() {
  const Samples c = constant(8, 500);
  Sound inst[2] = {Sound{c.data(), 8, 22050, Pcm16, 1}, Sound{c.data(), 8, 22050, Pcm8, 1}};
  PatternEvent ev[3] = {{0, 0, 0, 60, 255}, {1, 1, 1, 62, 255}, {1, 3, 0, 0, 0}};
  Pattern good = {ev, 3, 4, inst, 2, 120, 4};
  CHECK(g_a.init(22050) == Ok);
  CHECK(g_a.playMusic(good, true) == Ok);
  render(g_a, 4000);
  CHECK(g_a.musicPlaying());

  auto reject = [&](const Pattern& p) -> int {
    CHECK(g_a.playMusic(p, true) == InvalidArg);
    return 0;
  };
  Pattern p;
  p = good; p.rows = 0; CHECK(reject(p) == 0);
  p = good; p.bpm = 0; CHECK(reject(p) == 0);
  p = good; p.rows_per_beat = 0; CHECK(reject(p) == 0);
  p = good; p.events = nullptr; CHECK(reject(p) == 0);
  p = good; p.instruments = nullptr; CHECK(reject(p) == 0);
  p = good; p.rows = 1; CHECK(reject(p) == 0);  // events on row 1 >= rows
  { PatternEvent e[3] = {{1, 0, 0, 60, 255}, {0, 0, 0, 60, 255}, {2, 0, 0, 60, 255}}; p = good; p.events = e; CHECK(reject(p) == 0); }
  { PatternEvent e[1] = {{0, PGE_AUDIO_CHANNELS / 2, 0, 60, 255}}; p = good; p.events = e; p.event_count = 1; CHECK(reject(p) == 0); }
  { PatternEvent e[1] = {{0, 0, 0, 128, 255}}; p = good; p.events = e; p.event_count = 1; CHECK(reject(p) == 0); }
  { PatternEvent e[1] = {{0, 0, 2, 60, 255}}; p = good; p.events = e; p.event_count = 1; CHECK(reject(p) == 0); }
  const Sound bad[4] = {Sound{nullptr, 8, 22050, Pcm16, 0}, Sound{c.data(), 0, 22050, Pcm16, 0}, Sound{c.data(), 8, 0, Pcm16, 0},
                        Sound{c.data(), 8, 22050, 2, 0}};
  for (const Sound& b : bad) {
    Sound bi[2] = {inst[0], b};
    p = good; p.instruments = bi;
    CHECK(reject(p) == 0);
  }
  { PatternEvent e[1] = {{0, 0, 200, 0, 0}}; p = good; p.events = e; p.event_count = 1; CHECK(g_b.init(22050) == Ok); CHECK(g_b.playMusic(p, true) == Ok); }  // note off needs no instrument
  // the rejected calls queued nothing and did not disturb the running music: it matches an undisturbed twin
  CHECK(g_a.stats().dropped_cmds == 0 && g_a.musicPlaying());
  const Samples x = render(g_a, 2000);
  CHECK(g_b.init(22050) == Ok);
  CHECK(g_b.playMusic(good, true) == Ok);
  render(g_b, 4000);
  CHECK(x == render(g_b, 2000));
  return 0;
}

// ---------------------------------------------------------------- stream equivalence
static const Samples& rampA() { static Samples s = ramp(300, 91, -13000); return s; }
static const std::vector<int8_t>& rampB() {
  static std::vector<int8_t> s = [] { std::vector<int8_t> v(200); for (int i = 0; i < 200; i++) v[i] = (int8_t)(i - 100); return v; }();
  return s;
}

static Samples stream(Mixer& m, uint32_t chunk, uint32_t total) {
  static const Sound inst[2] = {Sound{rampA().data(), 300, 11025, Pcm16, 1}, Sound{rampB().data(), 200, 30000, Pcm8, 1}};
  static const PatternEvent ev[] = {{0, 0, 0, 60, 255}, {0, 1, 1, 64, 200}, {1, 2, 0, 72, 255}, {2, 0, 0, 0, 0},
                                    {2, 3, 1, 47, 180}, {3, 1, 0, 55, 100}, {4, 0, 1, 67, 255}};
  static const Pattern pat = {ev, 7, 5, inst, 2, 240, 8};
  m.init(22050);
  m.setMaster(230);
  m.play(snd16(rampA(), 22050), 255, true, 1);
  m.play(snd8(rampB(), 16000), 200, true, 0);
  const Voice dropme = m.play(snd16(rampA(), 7000), 255, true, 0);
  m.play(snd8(rampB(), 44100), 90, false, 2);
  m.play(snd16(rampA(), 12345), 255, false, 0);  // rejected or stolen: all slots busy
  m.play(snd8(rampB(), 22050), 255, true, 3);
  m.setVolume(dropme, 40);
  m.stop(dropme);
  m.playMusic(pat, true);
  Samples out;
  uint32_t done = 0;
  m.fill(nullptr, 0);
  while (done < total) {
    const uint32_t n = total - done < chunk ? total - done : chunk;
    const Samples o = render(m, n);
    out.insert(out.end(), o.begin(), o.end());
    done += n;
  }
  return out;
}

static int testStreamIndependence() {
  const uint32_t total = 5000;
  const Samples ref = stream(g_a, total, total);
  size_t nz = 0;
  for (int16_t v : ref) nz += v != 0;
  CHECK(nz > total / 2);
  const uint32_t sizes[] = {1, 7, 64, 127, 128, 129, 1000};
  for (uint32_t c : sizes) {
    const Samples o = stream(g_b, c, total);
    CHECK(o == ref);
  }
  CHECK(g_a.musicPlaying());
  return 0;
}

static int testDeterminism() {
  const Samples x = stream(g_a, 333, 4000), y = stream(g_b, 333, 4000);
  CHECK(x.size() == y.size() && std::memcmp(x.data(), y.data(), x.size() * 2) == 0);
  CHECK(g_a.stats().stolen == g_b.stats().stolen && g_a.stats().rejected == g_b.stats().rejected);
  return 0;
}

int main() {
  if (testInit()) return 1;
  if (testPcmExact()) return 1;
  if (testResample()) return 1;
  if (testGain()) return 1;
  if (testSumAndSaturation()) return 1;
  if (testEndAndLoop()) return 1;
  if (testStealing()) return 1;
  if (testQueue()) return 1;
  if (testStaleHandle()) return 1;
  if (testMusicTiming()) return 1;
  if (testMusicPitch()) return 1;
  if (testMusicAndSfx()) return 1;
  if (testPatternValidation()) return 1;
  if (testStreamIndependence()) return 1;
  if (testDeterminism()) return 1;
  std::printf("test_audio ok\n");
  return 0;
}
