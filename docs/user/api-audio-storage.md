# Audio and storage API reference

> Summary for later sessions
> - Covers: `core/include/pge/audio.h`, `storage.h`, `save.h`, `settings.h`, `drivers/storage_fatfs/pge_storage_fatfs.h`,
>   `drivers/storage_littlefs/pge_storage_littlefs.h`, `platform/host/host_storage.h`.
> - Audio: host-tested and heard on SDL. The F429 SAI backend is compile-only.
> - Storage: the FatFs and LittleFS backends are tested on fakes only. There is no block-device driver, so on the
>   F429 board SAVE/LOAD show NO STORAGE.
> - Signatures and comments below are copied from the headers; nothing else was consulted.

All C++ items live in namespace `pge`. The two driver headers and the host header are C (`extern "C"`).

## core/include/pge/audio.h

Software mixer: PCM8/PCM16 mono voices, nearest-sample resampling, a pattern sequencer, stereo int16 output. One
producer (game thread) talks to one consumer (`fill`, called from the platform audio context) through a lock-free
command ring; the audio side never allocates. The SMALL profile (0 channels) compiles the module out: everything is
inside `#if PGE_AUDIO_CHANNELS > 0`.

### Types

| Name | Definition | Meaning |
|---|---|---|
| `SoundFormat` | `enum : uint8_t { Pcm8 = 0, Pcm16 = 1 }` | Pcm8 = signed int8, Pcm16 = native-endian int16; both mono |
| `Sound` | `{ const void* data; uint32_t frames; uint16_t rate; uint8_t format; uint8_t loop; }` | `loop` is used only for pattern instruments; `Mixer::play` takes its own loop argument. `data` must outlive playback |
| `Voice` | `{ uint16_t id; bool valid() const; }` | `id` = ticket handed out by the game thread, 0 = invalid. The slot is chosen on the audio side, so the handle carries no slot |
| `PatternEvent` | `{ uint16_t row; uint8_t channel, instrument, note, volume; }` | note 0 = off, 1..127 MIDI-like, 60 plays the instrument at its own rate; volume 0..255 |
| `Pattern` | `{ const PatternEvent* events; uint16_t event_count, rows; const Sound* instruments; uint8_t instrument_count, bpm, rows_per_beat; }` | Events sorted by row. Pattern, events and instruments must outlive the music |
| `MixerStats` | `{ uint32_t dropped_cmds, rejected, stolen; }` | `dropped_cmds`: command queue full. `rejected`: no free voice and the new sound was not of higher priority. `stolen`: a lower-priority voice was stopped for a new one. `dropped_cmds` is written by the game side only; `rejected` and `stolen` by the audio side only |

`detail::kSemi[12]` (`extern const uint32_t`): round(65536 * 2^(i/12)).

### class Mixer

Constants:

- `static constexpr uint8_t kChannels = PGE_AUDIO_CHANNELS;`
- `static constexpr uint8_t kMusicVoices = PGE_AUDIO_CHANNELS / 2;` Slots [0, kMusicVoices) are music, the rest effects.

Lifecycle:

| Signature | Meaning |
|---|---|
| `Error init(uint32_t sample_rate)` | 8000..48000 Hz else `InvalidArg`. Resets all state, makes this the one active mixer, opens the HAL device (not started). The HAL result is returned; the mixer is usable for direct `fill()` calls even if the HAL failed. Call while stopped: it is not synchronised against a running `fill()` |
| `Error start()` | (no comment in header) |
| `Error stop()` | (no comment in header) |

Game side, effects:

| Signature | Meaning |
|---|---|
| `Voice play(const Sound& s, uint8_t volume = 255, bool loop = false, uint8_t priority = 0)` | Invalid `Voice` if the sound is invalid, the mixer is not initialised or the queue is full (`dropped_cmds++`) |
| `void stop(Voice v)` | Queue full: `dropped_cmds++` and the call is lost |
| `void setVolume(Voice v, uint8_t volume)` | (no comment in header) |
| `void setMaster(uint8_t volume)` | (no comment in header) |
| `bool isPlaying(Voice v) const` | Reads the tickets the audio side published: false until `fill()` drained the play command |

Game side, music:

| Signature | Meaning |
|---|---|
| `Error playMusic(const Pattern& p, bool loop)` | `InvalidArg` bad pattern, `NotSupported` without music voices, `NotInitialized` before `init`, `Busy` if the queue is full |
| `void stopMusic()` | (no comment in header) |
| `bool musicPlaying() const` | (no comment in header) |

Audio side:

| Signature | Meaning |
|---|---|
| `void fill(int16_t* stereo, uint32_t frames)` | First drains all queued commands, then mixes; any frame count, the output does not depend on how a stream is split into calls. An event on pattern row r starts at music frame ceil(r * spr_fp / 65536) where spr_fp = (mix_rate * 60 << 16) / (bpm * rows_per_beat) |
| `const MixerStats& stats() const` | (no comment in header) |

Configuration macros used by the header (values come from `pge/config.h`, not shown here): `PGE_AUDIO_CHANNELS`,
`PGE_AUDIO_QUEUE`, `PGE_AUDIO_MIX_FRAMES`. Each queued command
(`Cmd`) is 16 B on 32-bit targets.

## core/include/pge/storage.h

Storage layer over the C HAL (`pge/hal/storage.h`): small whole-file I/O plus the streamed, CRC-checked, two-slot save
file. The payload is never held whole in RAM; everything goes through one I/O buffer.

### Free function and callback types

| Name | Signature | Meaning |
|---|---|---|
| `crc32Update` | `uint32_t crc32Update(uint32_t crc, const void* data, uint32_t n)` | CRC-32/IEEE (reflected, poly 0xEDB88320), chainable: start with 0. `crc32Update(0, "123456789", 9) == 0xCBF43926` |
| `Producer` | `typedef Error (*Producer)(void* ctx, uint32_t offset, void* dst, uint32_t max, uint32_t* written)` | Fills `dst` with payload bytes [offset, offset + *written); `*written = 0` ends the payload, `*written <= max`. Called twice per save with the same results (the state it reads must not change during `saveAtomic`) |
| `Consumer` | `typedef Error (*Consumer)(void* ctx, uint32_t offset, const void* src, uint32_t n)` | Called with ascending offsets after the whole payload passed its CRC check |

### struct SaveInfo

`{ uint32_t world_seed, game_tick; uint16_t w_chunks, h_chunks; }`

### class Storage

| Signature | Meaning |
|---|---|
| `Error init(Arena& a, uint32_t io_bytes = 0)` | Allocates the I/O buffer (0 = `PGE_STORAGE_IO_BYTES`). `OutOfMemory` if the arena fails |
| `void setBuffer(void* buf, uint32_t bytes)` | Uses a caller buffer instead (tests use 1, 7, 33 bytes); bytes >= 1 |
| `Error mount()` | Remembers success. Until it succeeds the other calls return `NotSupported`; without a buffer, `NotInitialized` |
| `Error readFile(const char* path, void* buf, uint32_t cap, uint32_t& len)` | Whole small file. Longer than `cap` gives `OutOfMemory`; open failure gives `Io` |
| `Error writeFile(const char* path, const void* data, uint32_t len)` | Plain truncate + write |
| `Error saveAtomic(const char* path, const SaveInfo& info, Producer produce, void* ctx)` | Writes `path.tmp`, verifies it by read-back, then swaps it in keeping the previous good file as `path.bak`. Paths longer than `PGE_PATH_MAX - 5` characters give `InvalidArg`. A non-deterministic producer gives `Corrupt` |
| `Error loadSave(const char* path, SaveInfo& info, Consumer consume, void* ctx)` | Tries `path`, then `path.bak` if `path` failed before `consume` was called. CRC is verified before the first `consume`. A consume error is returned at once without fallback. `info` is filled before the first `consume` call |
| `bool exists(const char* path) const` | Open for read succeeds |

## core/include/pge/save.h

Save game: world change log plus an opaque game blob, streamed through `Storage`. Payload = sections
`u8 type, u8 flags (0), u32 len, body`; type 1 = world changes (always written), type 2 = game blob (only when
non-empty). Unknown section types are skipped on load.

### class ByteSink

Window over a logical byte stream. Default constructed = counting mode (stores nothing). With `(offset, dst, max)` it
stores only the stream bytes in [offset, offset + max); `pos()` still counts every byte put.

| Signature | Meaning |
|---|---|
| `ByteSink()` | Counting mode |
| `ByteSink(uint32_t offset, void* dst, uint32_t max)` | Windowed mode |
| `void put(uint8_t b)` | (no comment in header) |
| `void put16(uint16_t v)` | (no comment in header) |
| `void put32(uint32_t v)` | (no comment in header) |
| `void putVarint(uint32_t v)` | LEB128 |
| `void putBytes(const void* p, uint32_t n)` | (no comment in header) |
| `uint32_t pos() const` | All bytes put, inside the window or not |
| `uint32_t written() const` | Bytes stored into `dst` |
| `bool full() const` | Encoders may stop early |

### class ChangeReader

Byte-fed decoder of the type-1 body; fields may be split across `feed()` calls at any byte.

| Signature | Meaning |
|---|---|
| `void begin(World& w)` | Clears the world's change log |
| `Error feed(const uint8_t* p, uint32_t n)` | (no comment in header) |
| `Error end()` | `Corrupt` if a chunk, run list or run is unfinished |

### Functions

| Signature | Meaning |
|---|---|
| `Error writeChanges(const World& w, ByteSink& s)` | Body of a type-1 section: per changed chunk `u16 chunk, varint n_runs`, runs `varint loc_delta, varint run_len, u8 tile`. An empty log writes zero bytes. Returns early (`Ok`) once the sink is full |
| `Error saveGame(Storage& st, const char* path, const World& w, uint32_t game_tick, const void* blob, uint32_t blob_len)` | Saves the world's change log and an optional game blob (ignored when `blob == nullptr` or `blob_len == 0`) |
| `Error loadGame(Storage& st, const char* path, World& w, uint32_t& game_tick, void* blob, uint32_t blob_cap, uint32_t& blob_len)` | Loads a save into `w`, which must be the same world (seed and size in chunks) else `InvalidArg` and the world is untouched. After the call the caller must run `ensureLoaded` again. On ANY other error after the world was modified (`Corrupt`, `OutOfMemory`, consumer errors) the change log is cleared again, so the world is left as pure generator output, and `blob_len` is 0. On success `game_tick` is set and `blob_len` is the blob size (0 if the save has none) |

## core/include/pge/settings.h

`key = value` settings file. Fixed table allocated once from the init arena.

### class Settings

| Signature | Meaning |
|---|---|
| `Error init(Arena& a, uint16_t max_keys = 0)` | `max_keys` 0 = `PGE_CONFIG_MAX_KEYS`. `OutOfMemory` if the arena fails |
| `void parse(const char* text, uint32_t len)` | One "key = value" per line ('\n' or "\r\n"); spaces/tabs around key and value are trimmed; blank lines and lines starting with '#' are ignored. Lines without '=', with an empty key, a key > 23 or a value > 31 chars, or that do not fit into a full table are skipped and counted. A repeated key replaces its value. Never reads past `len` |
| `Error load(Storage& st, const char* path, void* scratch, uint32_t scratch_bytes)` | `readFile` into the caller's scratch, then `parse`. Returns `readFile`'s error; settings are untouched on error |
| `int32_t getInt(const char* key, int32_t def) const` | Decimal (optional +/-) or 0x hex; anything else, int32 overflow or an empty value gives `def` |
| `const char* getStr(const char* key, const char* def) const` | (no comment in header) |
| `uint16_t count() const` | (no comment in header) |
| `uint16_t skipped() const` | (no comment in header) |

## drivers/storage_fatfs/pge_storage_fatfs.h

FatFs backend for the storage HAL (includes `ff.h`). C linkage.

| Signature | Meaning |
|---|---|
| `int pge_fatfs_storage_bind(FATFS* fs, const char* drive)` | Binds the storage backend to a caller-owned `FATFS` work area and logical drive ("" or "0:"). Call before `pge_hal_storage_mount()`. Returns 0, or -1 if `fs` is null |

## drivers/storage_littlefs/pge_storage_littlefs.h

LittleFS backend for the storage HAL (includes `lfs.h`). C linkage.

| Name | Signature | Meaning |
|---|---|---|
| `PGE_LFS_CACHE_MAX` | `#ifndef PGE_LFS_CACHE_MAX` / `#define PGE_LFS_CACHE_MAX 256` | Largest `lfs_config.cache_size` the backend supports (one static buffer per open-file slot). |
| `pge_lfs_storage_bind` | `int pge_lfs_storage_bind(lfs_t* fs, const struct lfs_config* cfg)` | Binds the backend to a caller-owned `lfs_t` and a fully filled `lfs_config` (must outlive the backend). Call before `pge_hal_storage_mount()`. Returns 0, or -1 if a pointer is null or `cfg->cache_size` is too big |

## platform/host/host_storage.h

Test hooks of the host storage backend. C linkage.

| Signature | Meaning |
|---|---|
| `void pge_host_storage_set_budget(int32_t units)` | Power-loss simulation. `units` < 0 = unlimited (default). Otherwise each unit is one byte written, or one open-for-write / rename / remove call. When the budget is spent the operation fails with `Io`; a write that is only partly covered writes the covered bytes first, then returns `Io`. close and read are never limited |
| `int32_t pge_host_storage_open_files(void)` | Number of currently open handles (leak check) |
