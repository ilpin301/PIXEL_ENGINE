#include "pge/audio.h"
#include "pge/hal/audio.h"

#if PGE_AUDIO_CHANNELS > 0

namespace pge {

namespace detail {
const uint32_t kSemi[12] = {65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715};
}

namespace {

constexpr uint32_t kStepMax = 16u << 16;
constexpr uint8_t kFlagPcm16 = 1, kFlagLoop = 2;

Mixer* g_active;  // the mixer the HAL callback drives

void trampoline(int16_t* stereo, uint32_t frames) {
  if (g_active) g_active->fill(stereo, frames);
}

bool soundOk(const Sound& s) { return s.data && s.frames > 0 && s.rate > 0 && s.format <= Pcm16; }

uint32_t clampStep(uint64_t step) { return step < 1 ? 1 : step > kStepMax ? kStepMax : (uint32_t)step; }

uint8_t flagsOf(const Sound& s, bool loop) {
  return (uint8_t)((s.format == Pcm16 ? kFlagPcm16 : 0) | (loop ? kFlagLoop : 0));
}

bool patternOk(const Pattern& p) {
  if (p.rows < 1 || p.bpm < 1 || p.rows_per_beat < 1) return false;
  if ((p.event_count > 0 && !p.events) || (p.instrument_count > 0 && !p.instruments)) return false;
  uint16_t prev = 0;
  for (uint32_t i = 0; i < p.event_count; i++) {
    const PatternEvent& e = p.events[i];
    if (e.row >= p.rows || e.row < prev || e.channel >= Mixer::kMusicVoices || e.note > 127) return false;
    if (e.note != 0 && (e.instrument >= p.instrument_count || !soundOk(p.instruments[e.instrument]))) return false;
    prev = e.row;
  }
  return true;
}

}  // namespace

Error Mixer::init(uint32_t sample_rate) {
  if (sample_rate < 8000 || sample_rate > 48000) return InvalidArg;
  head_.store(0, std::memory_order_relaxed);
  tail_.store(0, std::memory_order_relaxed);
  for (uint8_t i = 0; i < kChannels; i++) {
    slot_[i] = Slot{};
    active_ticket_[i].store(0, std::memory_order_relaxed);
  }
  music_ = {};
  music_on_.store(false, std::memory_order_relaxed);
  next_ticket_ = 1;
  stats_ = {};
  mix_rate_ = sample_rate;
  age_ = 0;
  master_ = 255;
  inited_ = true;
  g_active = this;
  return (Error)pge_hal_audio_init(sample_rate, trampoline);
}

Error Mixer::start() { return (Error)pge_hal_audio_start(); }
Error Mixer::stop() { return (Error)pge_hal_audio_stop(); }

// ---------------------------------------------------------------- game side
bool Mixer::push(const Cmd& c) {
  const uint32_t h = head_.load(std::memory_order_relaxed);
  if (h - tail_.load(std::memory_order_acquire) >= PGE_AUDIO_QUEUE) {
    stats_.dropped_cmds++;
    return false;
  }
  q_[h % PGE_AUDIO_QUEUE] = c;
  head_.store(h + 1, std::memory_order_release);
  return true;
}

Voice Mixer::play(const Sound& s, uint8_t volume, bool loop, uint8_t priority) {
  if (!inited_ || !soundOk(s)) return Voice{0};
  if (!push(Cmd{OpPlay, volume, flagsOf(s, loop), priority, next_ticket_, s.rate, s.data, s.frames})) return Voice{0};
  const uint16_t t = next_ticket_;
  if (++next_ticket_ == 0) next_ticket_ = 1;
  return Voice{t};
}

void Mixer::stop(Voice v) {
  if (inited_ && v.valid()) push(Cmd{OpStop, 0, 0, 0, v.id, 0, nullptr, 0});
}

void Mixer::setVolume(Voice v, uint8_t volume) {
  if (inited_ && v.valid()) push(Cmd{OpVolume, volume, 0, 0, v.id, 0, nullptr, 0});
}

void Mixer::setMaster(uint8_t volume) {
  if (inited_) push(Cmd{OpMaster, volume, 0, 0, 0, 0, nullptr, 0});
}

bool Mixer::isPlaying(Voice v) const {
  if (!v.valid()) return false;
  for (uint8_t i = kMusicVoices; i < kChannels; i++)
    if (active_ticket_[i].load(std::memory_order_relaxed) == v.id) return true;
  return false;
}

Error Mixer::playMusic(const Pattern& p, bool loop) {
  if (kMusicVoices == 0) return NotSupported;
  if (!inited_) return NotInitialized;
  if (!patternOk(p)) return InvalidArg;
  return push(Cmd{OpMusic, 0, (uint8_t)(loop ? kFlagLoop : 0), 0, 0, 0, &p, 0}) ? Ok : Busy;
}

void Mixer::stopMusic() {
  if (inited_) push(Cmd{OpStopMusic, 0, 0, 0, 0, 0, nullptr, 0});
}

// ---------------------------------------------------------------- audio side: commands
uint32_t Mixer::stepFor(uint32_t rate) const { return clampStep(((uint64_t)rate << 16) / mix_rate_); }

void Mixer::freeSlot(uint8_t i) {
  slot_[i].data = nullptr;
  if (i >= kMusicVoices) active_ticket_[i].store(0, std::memory_order_relaxed);
}

Mixer::Slot* Mixer::find(uint16_t ticket) {
  for (uint8_t i = kMusicVoices; i < kChannels; i++)
    if (slot_[i].data && slot_[i].ticket == ticket) return &slot_[i];
  return nullptr;
}

void Mixer::startSlot(Slot& s, const void* data, uint32_t frames, uint32_t step, uint8_t vol, uint8_t flags,
                      uint8_t prio, uint16_t ticket) {
  s = Slot{data, frames, 0, step, 0, ticket, vol, flags, prio, ++age_};
}

void Mixer::startVoice(const Cmd& c) {
  int slot = -1;
  for (uint8_t i = kMusicVoices; i < kChannels && slot < 0; i++)
    if (!slot_[i].data) slot = i;
  if (slot < 0) {
    int victim = kMusicVoices;  // lowest priority, ties -> oldest
    for (uint8_t i = kMusicVoices + 1; i < kChannels; i++)
      if (slot_[i].prio < slot_[victim].prio || (slot_[i].prio == slot_[victim].prio && slot_[i].age < slot_[victim].age))
        victim = i;
    if (c.prio <= slot_[victim].prio) {
      stats_.rejected++;
      return;
    }
    stats_.stolen++;
    slot = victim;
  }
  startSlot(slot_[slot], c.ptr, c.frames, stepFor(c.rate), c.vol, c.flags, c.prio, c.ticket);
  active_ticket_[slot].store(c.ticket, std::memory_order_relaxed);
}

void Mixer::musicStop() {
  for (uint8_t i = 0; i < kMusicVoices; i++) freeSlot(i);
  music_.active = false;
  music_on_.store(false, std::memory_order_relaxed);
}

void Mixer::musicStart(const Pattern* p, bool loop) {
  musicStop();
  music_.pat = p;
  music_.row = 0;
  music_.next_event = 0;
  music_.loop = loop;
  music_.active = true;
  music_.spr_fp = (((uint64_t)mix_rate_ * 60) << 16) / ((uint32_t)p->bpm * p->rows_per_beat);
  music_.countdown = (int64_t)music_.spr_fp;
  music_on_.store(true, std::memory_order_relaxed);
  musicTrigger();
}

// Starts every event of the current row; events are sorted, so next_event only moves forward.
void Mixer::musicTrigger() {
  const Pattern& p = *music_.pat;
  for (; music_.next_event < p.event_count && p.events[music_.next_event].row == music_.row; music_.next_event++) {
    const PatternEvent& e = p.events[music_.next_event];
    if (e.note == 0) {
      freeSlot(e.channel);
      continue;
    }
    const Sound& s = p.instruments[e.instrument];
    const int d = (int)e.note - 60;
    const int oct = d >= 0 ? d / 12 : -((11 - d) / 12);  // floor division
    uint64_t step = ((uint64_t)stepFor(s.rate) * detail::kSemi[d - 12 * oct]) >> 16;
    step = oct >= 0 ? step << oct : step >> -oct;
    startSlot(slot_[e.channel], s.data, s.frames, clampStep(step), e.volume, flagsOf(s, s.loop != 0), 0, 0);
  }
}

// n frames of music time were mixed: move the row clock.
void Mixer::musicAdvance(uint32_t n) {
  music_.countdown -= (int64_t)n << 16;
  while (music_.active && music_.countdown <= 0) {
    if (++music_.row >= music_.pat->rows) {
      if (!music_.loop) {
        musicStop();
        break;
      }
      music_.row = 0;
      music_.next_event = 0;
    }
    musicTrigger();
    music_.countdown += (int64_t)music_.spr_fp;
  }
}

void Mixer::drain() {
  uint32_t t = tail_.load(std::memory_order_relaxed);
  while (t != head_.load(std::memory_order_acquire)) {
    const Cmd c = q_[t % PGE_AUDIO_QUEUE];
    switch (c.op) {
      case OpPlay: startVoice(c); break;
      case OpStop:
        if (Slot* s = find(c.ticket)) freeSlot((uint8_t)(s - slot_));
        break;
      case OpVolume:
        if (Slot* s = find(c.ticket)) s->vol = c.vol;
        break;
      case OpMaster: master_ = c.vol; break;
      case OpMusic: musicStart((const Pattern*)c.ptr, (c.flags & kFlagLoop) != 0); break;
      case OpStopMusic: musicStop(); break;
    }
    tail_.store(++t, std::memory_order_release);
  }
}

// ---------------------------------------------------------------- audio side: mixing
// Adds n frames of one voice into acc; false once a one-shot ran off its end. (s * (vol + 1)) >> 8: vol 255 is unity.
template <bool P16>
bool Mixer::mixSlot(Slot& s, int32_t* acc, uint32_t n) {
  const int16_t* d16 = (const int16_t*)s.data;
  const int8_t* d8 = (const int8_t*)s.data;
  const int32_t gain = (int32_t)s.vol + 1;
  const uint32_t whole = s.step >> 16, fr = s.step & 0xFFFF;
  for (uint32_t i = 0; i < n; i++) {
    const int32_t v = P16 ? d16[s.pos] : d8[s.pos] * 256;
    acc[i] += (v * gain) >> 8;
    const uint32_t f = (uint32_t)s.frac + fr;
    s.frac = (uint16_t)f;
    s.pos += whole + (f >> 16);
    if (s.pos >= s.frames) {
      if (!(s.flags & kFlagLoop)) return false;
      s.pos %= s.frames;
    }
  }
  return true;
}

void Mixer::fill(int16_t* out, uint32_t frames) {
  drain();
  while (frames) {
    uint32_t n = frames < PGE_AUDIO_MIX_FRAMES ? frames : PGE_AUDIO_MIX_FRAMES;
    if (music_.active) {  // end the chunk on the frame where the row clock crosses zero
      int64_t c = (music_.countdown + 65535) >> 16;
      if (c < 1) c = 1;
      if (n > c) n = (uint32_t)c;
    }
    for (uint32_t i = 0; i < n; i++) acc_[i] = 0;
    for (uint8_t v = 0; v < kChannels; v++) {
      Slot& s = slot_[v];
      if (!s.data) continue;
      const bool alive = (s.flags & kFlagPcm16) ? mixSlot<true>(s, acc_, n) : mixSlot<false>(s, acc_, n);
      if (!alive) freeSlot(v);
    }
    const int32_t m = (int32_t)master_ + 1;
    for (uint32_t i = 0; i < n; i++) {
      int32_t o = (acc_[i] * m) >> 8;  // |acc| <= 8 * 32768 and m <= 256: fits int32
      o = o > 32767 ? 32767 : o < -32768 ? -32768 : o;
      out[2 * i] = out[2 * i + 1] = (int16_t)o;
    }
    if (music_.active) musicAdvance(n);
    out += 2 * n;
    frames -= n;
  }
}

}  // namespace pge

#endif  // PGE_AUDIO_CHANNELS > 0
