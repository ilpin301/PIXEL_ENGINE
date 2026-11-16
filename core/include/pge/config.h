#pragma once
// Compile-time configuration. Order: optional game-side pge_user_config.h, then the board's pge_profile.h
// (exactly one PGE_PROFILE_* = 1, plus PGE_LOGICAL_W/H), then the defaults below. Every macro can be pre-set.
#if defined(__has_include)
#if __has_include("pge_user_config.h")
#include "pge_user_config.h"
#endif
#endif
#include "pge_profile.h"

#ifndef PGE_PROFILE_SMALL
#define PGE_PROFILE_SMALL 0
#endif
#ifndef PGE_PROFILE_MEDIUM
#define PGE_PROFILE_MEDIUM 0
#endif
#ifndef PGE_PROFILE_LARGE
#define PGE_PROFILE_LARGE 0
#endif
static_assert(PGE_PROFILE_SMALL + PGE_PROFILE_MEDIUM + PGE_PROFILE_LARGE == 1, "define exactly one PGE_PROFILE_*");

#define PGE_BY_PROFILE(s, m, l) (PGE_PROFILE_SMALL ? (s) : PGE_PROFILE_MEDIUM ? (m) : (l))

#ifndef PGE_LOGICAL_W
#define PGE_LOGICAL_W PGE_BY_PROFILE(160, 320, 320)
#endif
#ifndef PGE_LOGICAL_H
#define PGE_LOGICAL_H PGE_BY_PROFILE(128, 240, 240)
#endif
// 01-overview section 6 table. LARGE band is 32 lines (G3 accepted 32; 01 says 16).
#ifndef PGE_BAND_LINES
#define PGE_BAND_LINES PGE_BY_PROFILE(8, 16, 32)
#endif
#ifndef PGE_INTEGER_SCALE_MAX
#define PGE_INTEGER_SCALE_MAX PGE_BY_PROFILE(1, 2, 3)
#endif
#ifndef PGE_SCALE
#define PGE_SCALE 1  // K15: display = logical * PGE_SCALE
#endif
#ifndef PGE_SCALE_STAGING_BYTES
// 02 section 3.4: (N*s) * (W*s) * 2 with W logical; the 01-overview 5.3 table and its formula text understate this.
#define PGE_SCALE_STAGING_BYTES (PGE_SCALE > 1 ? PGE_BAND_LINES * PGE_SCALE * PGE_LOGICAL_W * PGE_SCALE * 2 : 0)
#endif
#ifndef PGE_PARALLAX_LAYERS
#define PGE_PARALLAX_LAYERS PGE_BY_PROFILE(1, 2, 4)
#endif
#ifndef PGE_CHUNK_CACHE_COUNT
#define PGE_CHUNK_CACHE_COUNT PGE_BY_PROFILE(4, 16, 64)
#endif
#ifndef PGE_MAX_CHUNK_LOADS_PER_TICK
#define PGE_MAX_CHUNK_LOADS_PER_TICK 1  // 03 section 2.2: chunk generations per ensureLoaded call
#endif
#ifndef PGE_STORAGE_IO_BYTES
#define PGE_STORAGE_IO_BYTES PGE_BY_PROFILE(512, 4096, 16384)  // 03 section 4.1: save/load streaming buffer
#endif
#ifndef PGE_PATH_MAX
#define PGE_PATH_MAX 64  // longest save path accepted is PGE_PATH_MAX - 5 characters (room for ".tmp"/".bak")
#endif
#ifndef PGE_CONFIG_MAX_KEYS
#define PGE_CONFIG_MAX_KEYS PGE_BY_PROFILE(8, 16, 32)  // 03 section 4.3: settings table entries
#endif
#ifndef PGE_CONFIG_KEY_BYTES
#define PGE_CONFIG_KEY_BYTES 24  // includes the terminating NUL
#endif
#ifndef PGE_CONFIG_VALUE_BYTES
#define PGE_CONFIG_VALUE_BYTES 32  // includes the terminating NUL
#endif
#ifndef PGE_AUDIO_CHANNELS
#define PGE_AUDIO_CHANNELS PGE_BY_PROFILE(0, 4, 8)
#endif
#ifndef PGE_AUDIO_QUEUE
#define PGE_AUDIO_QUEUE PGE_BY_PROFILE(8, 16, 32)  // game -> mixer command queue entries
#endif
#ifndef PGE_AUDIO_MIX_FRAMES
#define PGE_AUDIO_MIX_FRAMES 128  // accumulator block: frames mixed per pass
#endif
#ifndef PGE_LIGHTING_ENABLED
#define PGE_LIGHTING_ENABLED 0  // LARGE: optional, only after the S5.6 measurement
#endif
#ifndef PGE_DEBUG_OVERLAY
#define PGE_DEBUG_OVERLAY 1  // compiled out when 0 (drawOverlay becomes an empty function)
#endif
#ifndef PGE_PARTICLE_COUNT
#define PGE_PARTICLE_COUNT PGE_BY_PROFILE(32, 128, 512)
#endif

#ifndef PGE_ENTITY_COUNT
#define PGE_ENTITY_COUNT PGE_BY_PROFILE(64, 256, 1024)
#endif
#ifndef PGE_EVENT_QUEUE
#define PGE_EVENT_QUEUE PGE_BY_PROFILE(16, 32, 64)
#endif
#ifndef PGE_ENTITY_CELL_PX
#define PGE_ENTITY_CELL_PX 32  // overlap-grid cell edge in pixels
#endif
#ifndef PGE_ENTITY_GRID_CELLS
#define PGE_ENTITY_GRID_CELLS PGE_BY_PROFILE(42, 120, 168)  // u16 list heads; covers the active rect in cells
#endif

#ifndef PGE_TICK_HZ
#define PGE_TICK_HZ 60
#endif
#ifndef PGE_MAX_CATCHUP
#define PGE_MAX_CATCHUP 5  // ticks run per loop pass; the rest are dropped and counted
#endif
#ifndef PGE_POOL_KINDS
#define PGE_POOL_KINDS 8
#endif
#ifndef PGE_INIT_ARENA_BYTES
#define PGE_INIT_ARENA_BYTES (PGE_BY_PROFILE(8192, 32768, 65536) + PGE_SCALE_STAGING_BYTES)
#endif
#ifndef PGE_FRAME_ARENA_BYTES
#define PGE_FRAME_ARENA_BYTES PGE_BY_PROFILE(512, 2048, 4096)
#endif
#ifndef PGE_CLEAR_COLOR
#define PGE_CLEAR_COLOR 0
#endif
#ifndef PGE_GAME_SEED
#define PGE_GAME_SEED 0
#endif

// Placement: the platform profile maps these to sections/attributes; core only applies them.
#ifndef PGE_RAM_FAST
#define PGE_RAM_FAST
#endif
#ifndef PGE_RAM_DMA
#define PGE_RAM_DMA
#endif
#ifndef PGE_RAM_EXT
#define PGE_RAM_EXT
#endif
#ifndef PGE_ROM
#define PGE_ROM
#endif

static_assert(PGE_TICK_HZ >= 1 && PGE_TICK_HZ <= 1000, "PGE_TICK_HZ must be 1..1000");
static_assert(PGE_SCALE >= 1 && PGE_SCALE <= PGE_INTEGER_SCALE_MAX, "PGE_SCALE must be 1..PGE_INTEGER_SCALE_MAX");
static_assert(PGE_MAX_CATCHUP >= 1, "PGE_MAX_CATCHUP must be >= 1");
static_assert(PGE_BAND_LINES >= 1, "PGE_BAND_LINES must be >= 1");
static_assert(PGE_ENTITY_COUNT >= 1 && PGE_ENTITY_COUNT <= 65533, "PGE_ENTITY_COUNT must be 1..65533 (0xFFFF is the sentinel)");
static_assert(PGE_ENTITY_CELL_PX >= 1, "PGE_ENTITY_CELL_PX must be >= 1");
static_assert(PGE_LOGICAL_W > 0 && PGE_LOGICAL_H > 0, "logical size must be > 0");
