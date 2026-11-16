#pragma once
// Software mixer (docs/architecture/03 section 5): PCM8/PCM16 mono voices, nearest-sample resampling, a pattern
// sequencer, stereo int16 output. One producer (game thread) talks to one consumer (fill, called from the platform
// audio context) through a lock-free command ring; the audio side never allocates. SMALL profile (0 channels) compiles
// the module out.
#include <stdint.h>
#include <atomic>
#include "pge/config.h"
#include "pge/error.h"

#if PGE_AUDIO_CHANNELS > 0

namespace pge {

enum SoundFormat : uint8_t { Pcm8 = 0, Pcm16 = 1 };  // Pcm8 = signed int8, Pcm16 = native-endian int16; both mono

// loop is used only for pattern instruments; Mixer::play takes its own loop argument. data must outlive playback.
struct Sound {
  const void* data;
  uint32_t frames;
  uint16_t rate;
  uint8_t format;
  uint8_t loop;
};

// id = ticket handed out by the game thread, 0 = invalid. (Deviation from 03 section 5 {index,gen}: the slot is
// chosen on the audio side, so the handle carries no slot.)
struct Voice {
  uint16_t id;
  bool valid() const { return id != 0; }
};

// note 0 = off, 1..127 MIDI-like, 60 plays the instrument at its own rate; volume 0..255.
struct PatternEvent {
  uint16_t row;
  uint8_t channel, instrument, note, volume;
};

// Events sorted by row. Pattern, events and instruments must outlive the music.
struct Pattern {
  const PatternEvent* events;
  uint16_t event_count, rows;
  const Sound* instruments;
  uint8_t instrument_count, bpm, rows_per_beat;
};

// dropped_cmds is written by the game side only; rejected and stolen by the audio side only.
struct MixerStats {
  uint32_t dropped_cmds;  // command queue full
  uint32_t rejected;      // no free voice and the new sound was not of higher priority
  uint32_t stolen;        // a lower-priority voice was stopped for a new one
};

namespace detail {
extern const uint32_t kSemi[12];  // round(65536 * 2^(i/12))
}

class Mixer {
 public:
  static constexpr uint8_t kChannels = PGE_AUDIO_CHANNELS;
  static constexpr uint8_t kMusicVoices = PGE_AUDIO_CHANNELS / 2;  // slots [0,kMusicVoices) music, the rest effects

  // 8000..48000 Hz else InvalidArg. Resets all state, makes this the one active mixer, opens the HAL device (not
  // started). The HAL result is returned; the mixer is usable for direct fill() calls even if the HAL failed.
  // Call while stopped: it is not synchronised against a running fill().
  Error init(uint32_t sample_rate);
  Error start();
  Error stop();

  // Invalid Voice if the sound is invalid, the mixer is not initialised or the queue is full (dropped_cmds++).
  Voice play(const Sound& s, uint8_t volume = 255, bool loop = false, uint8_t priority = 0);
  // Queue full: dropped_cmds++ and the call is lost.
  void stop(Voice v);
  void setVolume(Voice v, uint8_t volume);
  void setMaster(uint8_t volume);
  // Reads the tickets the audio side published: false until fill() drained the play command.
  bool isPlaying(Voice v) const;

  // InvalidArg bad pattern, NotSupported without music voices, NotInitialized before init, Busy if the queue is full.
  Error playMusic(const Pattern& p, bool loop);
  void stopMusic();
  bool musicPlaying() const { return music_on_.load(std::memory_order_relaxed); }

  // Audio side. First drains all queued commands, then mixes; any frame count, the output does not depend on how a
  // stream is split into calls. Event on pattern row r starts at music frame ceil(r * spr_fp / 65536) where
  // spr_fp = (mix_rate * 60 << 16) / (bpm * rows_per_beat).
  void fill(int16_t* stereo, uint32_t frames);
  const MixerStats& stats() const { return stats_; }

 private:
  enum Op : uint8_t { OpPlay, OpStop, OpVolume, OpMaster, OpMusic, OpStopMusic };
  struct Cmd {  // 16 B on 32-bit targets
    uint8_t op, vol, flags, prio;
    uint16_t ticket, rate;
    const void* ptr;
    uint32_t frames;
  };
  struct Slot {
    const void* data;  // nullptr = free
    uint32_t frames, pos, step;
    uint16_t frac, ticket;
    uint8_t vol, flags, prio;
    uint32_t age;
  };

  bool push(const Cmd& c);
  void drain();
  void startVoice(const Cmd& c);
  void startSlot(Slot& s, const void* data, uint32_t frames, uint32_t step, uint8_t vol, uint8_t flags, uint8_t prio,
                 uint16_t ticket);
  void freeSlot(uint8_t i);
  Slot* find(uint16_t ticket);
  void musicStart(const Pattern* p, bool loop);
  void musicStop();
  void musicTrigger();
  void musicAdvance(uint32_t n);
  uint32_t stepFor(uint32_t rate) const;
  template <bool P16>
  static bool mixSlot(Slot& s, int32_t* acc, uint32_t n);

  std::atomic<uint32_t> head_{0}, tail_{0};  // free-running; head written by the game side, tail by the audio side
  Cmd q_[PGE_AUDIO_QUEUE];
  std::atomic<uint16_t> active_ticket_[kChannels];  // 0 = free
  std::atomic<bool> music_on_{false};
  uint16_t next_ticket_ = 1;  // game side
  bool inited_ = false;       // game side
  MixerStats stats_ = {};

  // audio side
  uint32_t mix_rate_ = 0, age_ = 0;
  uint8_t master_ = 255;
  Slot slot_[kChannels] = {};
  int32_t acc_[PGE_AUDIO_MIX_FRAMES];
  struct {
    const Pattern* pat;
    uint32_t next_event;
    uint16_t row;
    bool loop, active;
    int64_t countdown;  // 16.16 frames to the next row
    uint64_t spr_fp;    // 16.16 frames per row
  } music_ = {};
};

}  // namespace pge

#endif  // PGE_AUDIO_CHANNELS > 0
