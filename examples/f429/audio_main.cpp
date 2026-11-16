// pge_f429_audio: mixer CPU-cost bench (SAI stopped, Mixer::fill called directly), then 20 s of SAI1 playback
// (music pattern + one effect every 700 ms) through the PE4/PE5/PE6 I2S pins. Log via pge_dbg_*; "# AUDIO DONE" + LED4 = end.
#include <cmath>
#include <cstring>
#include "pge/audio.h"
#include "pge/hal/time.h"
#include "board.h"

extern "C" volatile uint32_t pge_f429_audio_irq_count;   // audio_sai.c

using namespace pge;

namespace {

const uint32_t kRate = 22050;
int16_t g_blip[2000];   // square wave, linear decay
int8_t g_noise[3000];   // decaying noise burst
int16_t g_sine[64];     // looping instrument cycle
int8_t g_saw[32];       // looping bass cycle
Sound g_inst[2];
PatternEvent g_ev[64 * 2];
Pattern g_pat;
Mixer g_mixer;
int16_t g_out[1024 * 2];

[[noreturn]] void fail(const char* what, int rc) {
  pge_dbg_printf("%s FAILED rc=%d\n", what, rc);
  pge_board_led(3, 1);
  for (;;) {}
}

void synth() {
  for (int i = 0; i < 2000; i++) g_blip[i] = (int16_t)(((i / 12) & 1 ? 12000 : -12000) * (2000 - i) / 2000);
  uint32_t lcg = 12345;
  for (int i = 0; i < 3000; i++) {
    lcg = lcg * 1664525u + 1013904223u;
    g_noise[i] = (int8_t)((int)((lcg >> 24) - 128) * (3000 - i) / 3000 / 2);
  }
  for (int i = 0; i < 64; i++) g_sine[i] = (int16_t)(9000 * std::sin(6.283185307 * i / 64));
  for (int i = 0; i < 32; i++) g_saw[i] = (int8_t)(i * 4 - 64);
  g_inst[0] = Sound{g_sine, 64, 16744, Pcm16, 1};   // note 60 plays one cycle at rate = cycle length * 261.6 Hz
  g_inst[1] = Sound{g_saw, 32, 8372, Pcm8, 1};

  // 8 bars of 16 rows: bass (channel 0) on the beat, lead (channel 1) arpeggio every 2 rows; events sorted by row.
  static const uint8_t kBass[4] = {36, 36, 43, 41}, kLead[8] = {72, 75, 79, 75, 77, 74, 70, 74};
  int m = 0;
  for (int row = 0; row < 128; row++) {
    if (row % 4 == 0)
      g_ev[m++] = PatternEvent{(uint16_t)row, 0, 1, (uint8_t)(kBass[(row / 32 + (row % 16) / 4) % 4] + 12), 200};
    if (row % 2 == 0) g_ev[m++] = PatternEvent{(uint16_t)row, 1, 0, kLead[(row / 2 + row / 16) % 8], 150};
  }
  g_pat = Pattern{g_ev, (uint16_t)m, 128, g_inst, 2, 140, 4};
}

// us = total for kFills fills of kFrames frames; all divisions in uint64.
void benchCase(bool p16, bool half, uint32_t voices) {
  enum { kFills = 100, kFrames = 512 };
  const uint32_t rate = half ? kRate / 2 : kRate;
  g_mixer.init(kRate);   // HAL result ignored on purpose: fill() works without the device, this resets the mixer
  const Sound snd = p16 ? Sound{g_blip, 2000, (uint16_t)rate, Pcm16, 1} : Sound{g_noise, 3000, (uint16_t)rate, Pcm8, 1};
  for (uint32_t i = 0; i < (voices > 4 ? 4u : voices); i++) g_mixer.play(snd, 255, true);
  static PatternEvent ev[4];
  static Sound inst;
  static Pattern pat;
  if (voices > 4) {   // 4 effects + 4 music voices (pattern row 0 starts all four, 60000 rows at 1 bpm: never ends)
    inst = snd;
    for (int c = 0; c < 4; c++) ev[c] = PatternEvent{0, (uint8_t)c, 0, 60, 255};
    pat = Pattern{ev, 4, 60000, &inst, 1, 1, 1};
    if (g_mixer.playMusic(pat, true) != Ok) fail("bench playMusic", -1);
  }
  g_mixer.fill(g_out, kFrames);   // drain the commands
  const uint32_t t0 = pge_hal_time_us();
  for (int f = 0; f < kFills; f++) g_mixer.fill(g_out, kFrames);
  const uint64_t us = pge_hal_time_us() - t0;
  const uint64_t vs = (uint64_t)kFills * kFrames * voices;   // voice-samples mixed
  const uint64_t ns = us * 1000 / vs;
  const uint64_t pm10 = us * kRate * 10 / ((uint64_t)kFills * kFrames * 1000);   // tenths of a permille of one core
  pge_dbg_printf("AUDIO %s r%s v%lu: %lu.%02lu us/fill, %lu ns/voice-sample, %lu.%lu permille of one core at %lu Hz, "
                 "%lu cyc/voice-sample\n",
                 p16 ? "PCM16" : "PCM8", half ? "0.5" : "1.0", (unsigned long)voices, (unsigned long)(us / kFills),
                 (unsigned long)(us % kFills), (unsigned long)ns, (unsigned long)(pm10 / 10),
                 (unsigned long)(pm10 % 10), (unsigned long)kRate, (unsigned long)(us * 180 / vs));
}

}  // namespace

int main() {
  pge_board_init();
  pge_dbg_puts("# pge_f429_audio: bench (SAI stopped), then 20 s playback on SAI1 PE4/PE5/PE6\n");
  synth();
  const Error e0 = g_mixer.init(kRate);
  if (e0 != Ok) fail("audio init", (int)e0);

  static const uint32_t kVoices[3] = {1, 4, 8};
  for (int f = 0; f < 2; f++)
    for (int h = 0; h < 2; h++)
      for (uint32_t v : kVoices) benchCase(f != 0, h != 0, v);

  const Error e1 = g_mixer.init(kRate);   // clean mixer state for playback
  if (e1 != Ok) fail("audio re-init", (int)e1);
  if (Error e = g_mixer.playMusic(g_pat, true); e != Ok) fail("playMusic", (int)e);
  if (Error e = g_mixer.start(); e != Ok) fail("audio start", (int)e);

  const Sound blip = {g_blip, 2000, (uint16_t)kRate, Pcm16, 0}, noise = {g_noise, 3000, (uint16_t)kRate, Pcm8, 0};
  const uint32_t t0 = pge_hal_time_us();
  uint32_t next_sfx = 0, shots = 0;
  while (pge_hal_time_us() - t0 < 20u * 1000000u) {
    if ((pge_hal_time_us() - t0) / 1000 >= next_sfx) {
      g_mixer.play((shots++ & 1) ? noise : blip, 220);
      next_sfx += 700;
    }
    pge_hal_time_delay_us(1000);
  }
  g_mixer.stop();
  pge_dbg_printf("AUDIO played %lu effects, irq_count=%lu, dropped %lu rejected %lu stolen %lu\n", (unsigned long)shots,
                 (unsigned long)pge_f429_audio_irq_count, (unsigned long)g_mixer.stats().dropped_cmds,
                 (unsigned long)g_mixer.stats().rejected, (unsigned long)g_mixer.stats().stolen);
  pge_dbg_puts("# AUDIO DONE\n");
  pge_board_led(4, 1);
  for (;;) {}
}
