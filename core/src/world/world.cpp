#include "pge/world.h"
#include <string.h>

namespace pge {

namespace {

int fdivPos(int a, int b) {  // floor division, b > 0
  int q = a / b;
  return (a % b != 0 && a < 0) ? q - 1 : q;
}

uint32_t makeKey(int chunk, Layer l, int tx, int ty) {
  return ((uint32_t)chunk << 16) | ((uint32_t)l << 10) | ((uint32_t)(ty & 31) << 5) | (uint32_t)(tx & 31);
}

}  // namespace

Error World::init(const WorldConfig& c, Arena& arena) {
  if (!c.gen || !c.props || c.w_chunks == 0 || c.h_chunks == 0 || (uint32_t)c.w_chunks * c.h_chunks > 65535 ||
      c.tile_px < 0)
    return InvalidArg;
  const uint16_t slots = c.cache_slots ? c.cache_slots : (uint16_t)PGE_CHUNK_CACHE_COUNT;
  const uint32_t cap = c.change_capacity ? c.change_capacity : 4096;
  uint8_t* data = arena.allocArray<uint8_t>((size_t)slots * 2 * kChunkTiles);
  int32_t* sc = arena.allocArray<int32_t>(slots);
  uint32_t* ss = arena.allocArray<uint32_t>(slots);
  uint32_t* key = arena.allocArray<uint32_t>(cap);
  uint8_t* tile = arena.allocArray<uint8_t>(cap);
  if (!data || !sc || !ss || !key || !tile) return OutOfMemory;
  for (int i = 0; i < slots; i++) { sc[i] = -1; ss[i] = 0; }
  w_chunks_ = c.w_chunks; h_chunks_ = c.h_chunks; slots_ = slots; loaded_ = 0; last_hit_ = 0;
  tile_px_ = c.tile_px ? c.tile_px : 16;
  seed_ = c.seed; tick_ = 0;
  gen_ = c.gen; on_activate_ = c.on_activate; user_ = c.user; props_ = c.props;
  data_ = data; slot_chunk_ = sc; slot_stamp_ = ss; key_ = key; tile_ = tile;
  count_ = 0; cap_ = cap;
  return Ok;
}

bool World::inBounds(int tx, int ty) const {
  return tx >= 0 && ty >= 0 && tx < (int)w_chunks_ * kChunkSize && ty < (int)h_chunks_ * kChunkSize;
}

// shortcut: linear scan over slots plus a last-hit index, upgrade to a map if profiling asks.
int World::findSlot(int chunk) const {
  if (slots_ == 0) return -1;
  if (slot_chunk_[last_hit_] == chunk) return last_hit_;
  for (int i = 0; i < slots_; i++)
    if (slot_chunk_[i] == chunk) { last_hit_ = (uint16_t)i; return i; }
  return -1;
}

bool World::isLoaded(int cx, int cy) const {
  if (cx < 0 || cy < 0 || cx >= w_chunks_ || cy >= h_chunks_) return false;
  return findSlot(cy * w_chunks_ + cx) >= 0;
}

uint8_t World::get(int tx, int ty, Layer l) const {
  if (!inBounds(tx, ty)) return kTileVoid;
  const int s = findSlot((ty >> 5) * w_chunks_ + (tx >> 5));
  if (s < 0) return kTileVoid;
  return data_[(size_t)s * 2 * kChunkTiles + (size_t)l * kChunkTiles + (size_t)(((ty & 31) << 5) | (tx & 31))];
}

uint32_t World::tileFlags(int tx, int ty) const {
  const uint8_t t = get(tx, ty, Layer::Fg);
  return t == kTileVoid ? (uint32_t)kTileSolid : (uint32_t)props_[t].flags;
}

int World::logFind(uint32_t key) const {
  int lo = 0, hi = (int)count_;
  while (lo < hi) {
    const int mid = (lo + hi) / 2;
    if (key_[mid] < key) lo = mid + 1; else hi = mid;
  }
  return lo;
}

Error World::set(int tx, int ty, Layer l, uint8_t t) {
  if (!inBounds(tx, ty) || t == kTileVoid) return InvalidArg;
  const int chunk = (ty >> 5) * w_chunks_ + (tx >> 5);
  const uint32_t key = makeKey(chunk, l, tx, ty);
  const int i = logFind(key);
  if (i < (int)count_ && key_[i] == key) {
    tile_[i] = t;
  } else {
    if (count_ >= cap_) return OutOfMemory;
    // shortcut: O(n) insert, upgrade if measured > 1 ms.
    const size_t n = count_ - (size_t)i;
    memmove(key_ + i + 1, key_ + i, n * sizeof(uint32_t));
    memmove(tile_ + i + 1, tile_ + i, n);
    key_[i] = key;
    tile_[i] = t;
    count_++;
  }
  const int s = findSlot(chunk);
  if (s >= 0)
    data_[(size_t)s * 2 * kChunkTiles + (size_t)l * kChunkTiles + (size_t)(((ty & 31) << 5) | (tx & 31))] = t;
  return Ok;
}

void World::load(int slot, int cx, int cy, int chunk) {
  uint8_t* fg = data_ + (size_t)slot * 2 * kChunkTiles;
  uint8_t* wall = fg + kChunkTiles;
  if (slot_chunk_[slot] < 0) loaded_++;
  slot_chunk_[slot] = chunk;
  slot_stamp_[slot] = tick_;
  gen_(fg, wall, cx, cy, seed_);
  for (int i = logFind((uint32_t)chunk << 16); i < (int)count_ && (key_[i] >> 16) == (uint32_t)chunk; i++) {
    const uint32_t loc = key_[i] & 0xFFFF;
    (loc & 0x400 ? wall : fg)[loc & 0x3FF] = tile_[i];
  }
}

int World::ensureLoaded(Rect v, int max_loads) {
  if (v.w <= 0 || v.h <= 0 || slots_ == 0) return 0;
  tick_++;
  const int maxx = (int)w_chunks_ * kChunkSize - 1, maxy = (int)h_chunks_ * kChunkSize - 1;
  int tx0 = fdivPos(v.x, tile_px_), tx1 = fdivPos(v.x + v.w - 1, tile_px_);
  int ty0 = fdivPos(v.y, tile_px_), ty1 = fdivPos(v.y + v.h - 1, tile_px_);
  if (tx1 < 0 || ty1 < 0 || tx0 > maxx || ty0 > maxy) return 0;
  if (tx0 < 0) tx0 = 0;
  if (ty0 < 0) ty0 = 0;
  if (tx1 > maxx) tx1 = maxx;
  if (ty1 > maxy) ty1 = maxy;
  const int cx0 = tx0 >> 5, cx1 = tx1 >> 5, cy0 = ty0 >> 5, cy1 = ty1 >> 5;

  // Pass 1: stamp needed chunks that are already loaded so pass 2 cannot evict them.
  for (int cy = cy0; cy <= cy1; cy++)
    for (int cx = cx0; cx <= cx1; cx++) {
      const int s = findSlot(cy * w_chunks_ + cx);
      if (s >= 0) slot_stamp_[s] = tick_;
    }

  int missing = 0, loads = 0;
  bool can_load = true;
  for (int cy = cy0; cy <= cy1; cy++)
    for (int cx = cx0; cx <= cx1; cx++) {
      const int chunk = cy * w_chunks_ + cx;
      if (findSlot(chunk) >= 0) continue;
      if (!can_load || loads >= max_loads) { missing++; continue; }
      int slot = -1;
      uint32_t best = 0;
      for (int i = 0; i < slots_; i++) {
        if (slot_chunk_[i] < 0) { slot = i; break; }
        if (slot_stamp_[i] != tick_ && (slot < 0 || slot_stamp_[i] < best)) { slot = i; best = slot_stamp_[i]; }
      }
      if (slot < 0) { can_load = false; missing++; continue; }
      load(slot, cx, cy, chunk);
      loads++;
    }
  return missing;
}

bool World::activate(int tx, int ty) {
  if (!on_activate_ || !inBounds(tx, ty)) return false;
  const uint8_t t = get(tx, ty, Layer::Fg);
  if (t == kTileVoid || !(props_[t].flags & kTileActivatable)) return false;
  on_activate_(*this, tx, ty, t, user_);
  return true;
}

void flatGen(uint8_t* fg, uint8_t* wall, int cx, int cy, uint32_t seed) {
  (void)cx; (void)seed;
  for (int ly = 0; ly < kChunkSize; ly++) {
    const int row = cy * kChunkSize + ly;
    const uint8_t f = row < kFlatSurfaceRow ? 0 : row == kFlatSurfaceRow ? 1 : 2;
    const uint8_t w = row > kFlatSurfaceRow ? 3 : 0;
    memset(fg + ly * kChunkSize, f, kChunkSize);
    memset(wall + ly * kChunkSize, w, kChunkSize);
  }
}

}  // namespace pge
