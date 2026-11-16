// pge_audiodemo [--seconds N] [--bench]
// Default: plays a 2-channel pattern plus one effect every 700 ms for N seconds (default 6) at 22050 Hz.
// --bench: no playback; drives Mixer::fill directly and prints the mixing cost per voice-sample.
// All sounds are synthesized at start-up, there are no asset files.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "pge/audio.h"
#include "pge/hal/time.h"

using namespace pge;

static const uint32_t kRate = 22050;

static int16_t g_blip[2000];  // square wave with a linear decay, ~880 Hz
static int8_t g_noise[3000];  // decaying noise burst
static int16_t g_sine[64];    // one looping cycle
static int8_t g_saw[32];      // one looping cycle

static Sound g_inst[2];
static PatternEvent g_ev[64 * 2];
static Pattern g_pat;

static void synth() {
  for (int i = 0; i < 2000; i++) g_blip[i] = (int16_t)(((i / 12) & 1 ? 12000 : -12000) * (2000 - i) / 2000);
  uint32_t lcg = 12345;
  for (int i = 0; i < 3000; i++) {
    lcg = lcg * 1664525u + 1013904223u;
    g_noise[i] = (int8_t)((int)((lcg >> 24) - 128) * (3000 - i) / 3000 / 2);
  }
  // Base pitch: note 60 plays one cycle at rate = cycle_length * 261.6 Hz.
  for (int i = 0; i < 64; i++) g_sine[i] = (int16_t)(9000 * std::sin(6.283185307 * i / 64));
  for (int i = 0; i < 32; i++) g_saw[i] = (int8_t)(i * 4 - 64);
  g_inst[0] = Sound{g_sine, 64, 16744, Pcm16, 1};
  g_inst[1] = Sound{g_saw, 32, 8372, Pcm8, 1};

  // 8 bars of 16 rows: bass (channel 0) on the beat, lead (channel 1) arpeggio every 2 rows.
  static const uint8_t kBass[4] = {36, 36, 43, 41};
  static const uint8_t kLead[8] = {72, 75, 79, 75, 77, 74, 70, 74};
  int n = 0;
  for (int bar = 0; bar < 8; bar++) {
    for (int beat = 0; beat < 4; beat++) {
      const int row = bar * 16 + beat * 4;
      g_ev[n++] = PatternEvent{(uint16_t)row, 0, 1, (uint8_t)(kBass[(bar / 2 + beat) % 4] + 12), 200};
    }
  }
  // Events must be sorted by row, so merge the lead notes in a second pass.
  PatternEvent merged[64 * 2];
  int m = 0, b = 0;
  for (int row = 0; row < 128; row++) {
    while (b < n && g_ev[b].row == row) merged[m++] = g_ev[b++];
    if (row % 2 == 0) merged[m++] = PatternEvent{(uint16_t)row, 1, 0, kLead[(row / 2 + row / 16) % 8], 150};
  }
  memcpy(g_ev, merged, sizeof(PatternEvent) * m);
  g_pat = Pattern{g_ev, (uint16_t)m, 128, g_inst, 2, 140, 4};
}

static int runRealtime(int seconds) {
  static Mixer mixer;
  Error e = mixer.init(kRate);
  if (e != Ok) { std::printf("audio init failed: %d\n", (int)e); return 1; }
  if ((e = mixer.playMusic(g_pat, true)) != Ok) { std::printf("playMusic failed: %d\n", (int)e); return 1; }
  if ((e = mixer.start()) != Ok) { std::printf("audio start failed: %d\n", (int)e); return 1; }
  const Sound blip = {g_blip, 2000, kRate, Pcm16, 0}, noise = {g_noise, 3000, kRate, Pcm8, 0};
  const uint32_t t0 = pge_hal_time_us();
  uint32_t next_sfx = 0;
  int shots = 0;
  while (pge_hal_time_us() - t0 < (uint32_t)seconds * 1000000u) {
    const uint32_t ms = (pge_hal_time_us() - t0) / 1000;
    if (ms >= next_sfx) {
      mixer.play((shots++ & 1) ? noise : blip, 220);
      next_sfx += 700;
    }
    pge_hal_time_delay_us(1000);
  }
  mixer.stop();
  std::printf("played %d effects, music %s, dropped %u rejected %u stolen %u\n", shots,
              mixer.musicPlaying() ? "playing" : "stopped", mixer.stats().dropped_cmds, mixer.stats().rejected,
              mixer.stats().stolen);
  return 0;
}

static int runBench() {
  static Mixer mixer;
  static int16_t out[2 * 1024];
  static int16_t loop16[4096];
  static int8_t loop8[4096];
  for (int i = 0; i < 4096; i++) { loop16[i] = (int16_t)((i * 37) % 20000 - 10000); loop8[i] = (int8_t)((i * 5) % 100 - 50); }
  const uint32_t voices[3] = {1, 4, 8};
  const uint32_t frames_total = 400u * 1024u;
  std::printf("bench: %u fills of 1024 frames per case, mix rate %u Hz\n", 400u, kRate);
  for (int fmt = 0; fmt < 2; fmt++) {
    for (int half = 0; half < 2; half++) {
      for (int vi = 0; vi < 3; vi++) {
        const uint32_t rate = half ? kRate / 2 : kRate;
        // The HAL result is ignored on purpose: the mixer works for direct fill() calls even without a device.
        mixer.init(kRate);
        const Sound snd = {fmt ? (const void*)loop16 : (const void*)loop8, 4096, (uint16_t)rate, (uint8_t)fmt, 1};
        const uint32_t sfx = voices[vi] > 4 ? 4 : voices[vi];
        for (uint32_t i = 0; i < sfx; i++) mixer.play(snd, 255, true);
        if (voices[vi] > 4) {
          static PatternEvent ev[4];
          static Pattern pat;
          static Sound inst;
          inst = snd;
          for (int c = 0; c < 4; c++) ev[c] = PatternEvent{0, (uint8_t)c, 0, 60, 255};
          pat = Pattern{ev, 4, 60000, &inst, 1, 1, 1};
          if (mixer.playMusic(pat, true) != Ok) { std::printf("bench: playMusic failed\n"); return 1; }
        }
        mixer.fill(out, 1024);  // drain the commands and warm up
        uint32_t us = 0;
        for (int f = 0; f < 400; f++) {
          const uint32_t a = pge_hal_time_us();
          mixer.fill(out, 1024);
          us += pge_hal_time_us() - a;
        }
        const double ns = (double)us * 1000.0 / ((double)frames_total * voices[vi]);
        const double pct = (double)us * 1e-6 / ((double)frames_total / kRate) * 100.0;
        std::printf("  %s ratio %.1f voices %u: %.2f ns/voice-sample, %.3f%% of one core at %u Hz\n",
                    fmt ? "PCM16" : "PCM8 ", half ? 0.5 : 1.0, voices[vi], ns, pct, kRate);
      }
    }
  }
  return 0;
}

int main(int argc, char** argv) {
  int seconds = 6;
  bool bench = false;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--bench")) bench = true;
  }
  synth();
  const int rc = bench ? runBench() : runRealtime(seconds);
  if (rc == 0) std::printf("audiodemo ok\n");
  return rc;
}
