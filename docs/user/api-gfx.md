# Pixel Game Engine: Graphics API Reference

> Summary for later sessions
> - Covers 6 headers under `core/include/pge/`: `gfx.h`, `color.h`, `sprite.h`, `camera.h`, `ui.h`, `particles.h`.
> - All identifiers are in namespace `pge` (UI widgets in `pge::ui`).
> - `gfx.h`: Color, Rect, SpriteFlags, SpriteSheet, Tileset, TileLayer, Font, Canvas, RenderConfig, renderFrame, turnRect.
> - `color.h`: rgb(), red8/green8/blue8, kPalette16.
> - `sprite.h`: Animation, animFrame. `camera.h`: Camera, clampAxis, parallaxOffset, MapLayer, drawLayers, orientForGravity.
> - `ui.h`: Style, label, bar, iconGrid, listMenu, formatU32, MenuNav, Toast, ToastQueue, toasts. `particles.h`: Particle, Particles.
> - Source of truth is the header comments; this file only restates them.

## `pge/gfx.h`

Core drawing types and the `Canvas` that every render callback draws into, plus `renderFrame`, which draws one frame band by band through the display HAL.

### Types

| Name | Definition | Meaning |
|---|---|---|
| `Color` | `typedef uint16_t Color;` | RGB565 |
| `Rect` | `struct Rect { int x, y, w, h; };` | |
| `SpriteFlags` | `enum SpriteFlags : uint8_t { None = 0, FlipX = 1, FlipY = 2, Rot90 = 4, Rot180 = 8, Rot270 = 12 };` | Flips apply to the stored image first, then the result is rotated clockwise by the field in bits 2-3 (use at most one of Rot90 / Rot180 / Rot270). With Rot90 / Rot270 the drawn rectangle is h wide and w tall; (x, y) is always its top-left corner. |
| `operator\|` | `constexpr SpriteFlags operator\|(SpriteFlags a, SpriteFlags b)` | Combines two flags. |
| `SpriteSheet` | `struct SpriteSheet { const uint8_t* px; const Color* palette; uint16_t w, h, frames; bool transparent; uint16_t cols = 0; };` | 8-bit indexed; index 0 is transparent when `transparent` is true. `cols == 0`: frames are stored one after another, frame f at `px + f*w*h`. `cols > 0`: an atlas with `cols` frames per row (row stride `cols*w`), frame f at column `f % cols`, row `f / cols`. |
| `Tileset` | `struct Tileset { const uint8_t* px; const Color* palette; uint8_t tile_w, tile_h; bool transparent = false; };` | Tile t at `px + t*tile_w*tile_h`; indexed. With `transparent` pixel index 0 is skipped (layers drawn over others), otherwise every pixel is written. Tile ids are not range-checked (no tile count yet). |
| `TileLayer` | `struct TileLayer { const uint8_t* tiles; uint16_t w, h; const Tileset* set; };` | |
| `Font` | `struct Font { const uint8_t* rows; uint8_t w, h, first, count, spacing; const FontRange* ranges = nullptr; uint8_t nranges = 0; };` | Base block: codepoint `first .. first+count-1` is glyph `ch - first`. Other codepoints are looked up in `ranges` (sorted, non-overlapping, linear scan) and use glyph `range.glyph + (ch - range.first)`. A glyph is h bytes at `rows + glyph*h`, one byte per glyph row; bit `(w - 1 - x)` is pixel x (w <= 8). A codepoint in neither draws `?`. |
| `FontRange` | `struct FontRange { uint16_t first, count, glyph; };` | Codepoints `first .. first+count-1` use glyphs `glyph .. glyph+count-1` (absolute glyph index). Used by `Font::ranges`; the same layout is the table entry in an asset font blob. |
| `RenderConfig` | `struct RenderConfig { uint16_t logical_w, logical_h; uint16_t band_lines; Color clear_color; uint8_t scale = 1; Color* stage = nullptr; uint32_t stage_pixels = 0; };` | `scale > 1` needs `stage` of at least `band_lines*scale * logical_w*scale` pixels. The window origin of the 02 spec is not implemented (deferred to the windowed flush, S5.6): the logical screen always fills the display. |
| `RenderFn` | `typedef void (*RenderFn)(Canvas&, void* ctx);` | Per-frame draw callback. |

### Constants and globals

| Name | Signature | Meaning |
|---|---|---|
| `kFont5x7` | `extern const Font kFont5x7;` | ASCII 32..126, plus German `Ä Ö Ü ä ö ü ß`, Cyrillic `А..я`, `Ё`, `ё` (73 glyphs in the range table). Accented capitals have their marks in row 0, so they are a pixel shorter. |
| `kMaxCoord` | `constexpr int kMaxCoord = 16384;` | line / circle / text arguments beyond this are dropped and counted |
| `kMaxScale` | `constexpr int kMaxScale = 3;` | |

### `class Canvas`

Valid only inside a `RenderFn`; made by `renderFrame`. All coordinates are logical-screen coordinates; everything is clipped to the current band and the screen.

| Member | Signature | Meaning |
|---|---|---|
| constructor | `Canvas(Color* buf, int w, int h, int band_y, int band_h, uint32_t seed);` | |
| `width` | `int width() const` | |
| `height` | `int height() const` | |
| `frameSeed` | `uint32_t frameSeed() const` | |
| `visible` | `bool visible(Rect r) const;` | intersects the current band and the screen |
| `clear` | `void clear(Color c);` | |
| `fillRect` | `void fillRect(Rect r, Color c);` | |
| `pixel` | `void pixel(int x, int y, Color c)` | |
| `line` | `void line(int x0, int y0, int x1, int y1, Color c);` | both ends inclusive |
| `rect` | `void rect(Rect r, Color c);` | 1 px outline inside r |
| `circle` | `void circle(int cx, int cy, int r, Color c);` | |
| `fillCircle` | `void fillCircle(int cx, int cy, int r, Color c);` | |
| `text` | `void text(const Font& f, int x, int y, const char* s, Color c);` | `s` is UTF-8; transparent background, newline starts a line. An invalid byte draws one `?` and consumes one byte; a codepoint not in the font draws `?`. |
| `dropped` | `uint16_t dropped() const` | invalid-argument draws in this canvas (saturating) |
| `sprite` | `void sprite(const SpriteSheet& s, uint16_t frame, int x, int y, SpriteFlags f);` | |
| `tilemap` | `void tilemap(const TileLayer& l, int cam_x, int cam_y, uint8_t orient = 0);` | `cam_x`, `cam_y` are the world pixel at the top-left of the world rectangle the screen shows (the same meaning as `Camera` x, y, for every `orient`). `orient` is the number of clockwise quarter turns of the picture, 0..3 (same numbering as the rotation field of `SpriteFlags`). See `camera.h` for Camera and parallax layers. |
| `tilemapRotated` | `void tilemapRotated(const TileLayer& l, int ax, int ay, int sx, int sy, int cos_q14, int sin_q14);` | The layer picture turned clockwise by any angle about the layer-pixel point (`ax`, `ay`), which lands at screen point (`sx`, `sy`). The angle is a unit vector in Q14 (16384 = 1.0); `(0, 16384)` equals `tilemap` with `orient` 1 at a matching camera. Screen pixels that fall outside the layer are not touched. `tile_w` and `tile_h` must be powers of two, the layer at most 32767 px per side and `cos_q14`, `sin_q14` within +-16384, else the call is dropped and counted. Build the vector from any (x, y) with a rounded integer square root: `r = isqrt(x*x + y*y)`, `cos = x * 16384 / r`, `sin = y * 16384 / r`; use a vector of length at least about 256 (scale a small one up first), then the result is a unit vector within about 0.5 percent (a truncated root or a short vector such as (1, 1) is not). |

Drop counting: invalid-argument draws are counted only on the band that starts at row 0, so one bad call per frame counts once, not once per band.

### Free functions

| Signature | Meaning |
|---|---|
| `int textWidth(const Font& f, const char* s);` | Widest line of s in pixels (no trailing spacing); counts glyphs (decoded UTF-8 codepoints, an invalid byte counts as one), not bytes; 0 for null or empty. |
| `int renderFrame(const RenderConfig& cfg, Color* band_buf, uint32_t band_buf_pixels, uint32_t seed, RenderFn fn, void* ctx);` | Draws one frame band by band through the display HAL (K2). Returns Ok, InvalidArg, or a nonzero flush/wait result. `band_lines > logical_h` is clamped; the last band may be shorter. Flush rects are in display coordinates (logical * scale). |
| `turnRect` | `Rect turnRect(Rect r, int view_w, int view_h, int quarter_turns);` | Turns `r`, given relative to the top-left of a view of `view_w` x `view_h`, by `quarter_turns` clockwise quarter turns (0..3). The result is relative to the top-left of the turned picture; w and h swap for odd `quarter_turns`. |

## `pge/color.h`

RGB565 helpers and a built-in 16-entry palette. Includes `pge/gfx.h`.

| Name | Signature | Meaning |
|---|---|---|
| `rgb` | `constexpr Color rgb(uint8_t r, uint8_t g, uint8_t b)` | |
| `red8` | `constexpr uint8_t red8(Color c)` | Expand to 8 bits by replicating the high bits, so 0xFFFF -> 255. |
| `green8` | `constexpr uint8_t green8(Color c)` | Same expansion as `red8`. |
| `blue8` | `constexpr uint8_t blue8(Color c)` | Same expansion as `red8`. |
| `kPalette16` | `inline constexpr Color kPalette16[16]` | See below. |

A palette is just a `const Color[]` (`SpriteSheet` and `Tileset` take `const Color*`); there is no Palette class.

`kPalette16` entries by index:

| Index | rgb | Index | rgb |
|---|---|---|---|
| 0 | `rgb(0, 0, 0)` | 8 | `rgb(64, 64, 64)` |
| 1 | `rgb(255, 255, 255)` | 9 | `rgb(128, 128, 128)` |
| 2 | `rgb(255, 0, 0)` | 10 | `rgb(192, 192, 192)` |
| 3 | `rgb(0, 255, 0)` | 11 | `rgb(255, 128, 0)` |
| 4 | `rgb(0, 0, 255)` | 12 | `rgb(128, 64, 0)` |
| 5 | `rgb(255, 255, 0)` | 13 | `rgb(0, 128, 0)` |
| 6 | `rgb(0, 255, 255)` | 14 | `rgb(0, 0, 128)` |
| 7 | `rgb(255, 0, 255)` | 15 | `rgb(255, 128, 192)` |

## `pge/sprite.h`

Frame-sequence animation for a `SpriteSheet`.

| Name | Signature | Meaning |
|---|---|---|
| `Animation` | `struct Animation { uint16_t first, count, ticks_per_frame; bool loop; };` | A frame sequence of a SpriteSheet: frames `first .. first + count - 1`, each shown for `ticks_per_frame` ticks. |
| `animFrame` | `inline uint16_t animFrame(const Animation& a, uint32_t ticks)` | Frame to draw `ticks` ticks after the animation started. A pure function of the tick count, so every band of one frame draws the same frame. Not looping: holds the last frame. `count == 0` gives `first`; `ticks_per_frame == 0` counts as 1. |

## `pge/camera.h`

Camera clamping, parallax offsets and multi-layer tile drawing. Includes `pge/gfx.h`.

### `struct Camera`

Camera position (x, y) is the top-left world pixel of the world rectangle the screen shows. The view and world sizes and the orientation follow. All in pixels.

| Member | Signature | Meaning |
|---|---|---|
| fields | `int x = 0, y = 0; int view_w = 0, view_h = 0; int world_w = 0, world_h = 0; uint8_t orient = 0;` | `x`, `y`: top-left of the shown world rectangle, in world pixels (same for every `orient`). `view_w`, `view_h`: screen size. `world_w`, `world_h`: world size. `orient`: clockwise quarter turns of the picture, 0..3. |
| `visW` | `int visW() const` | Width in world pixels of the shown world rectangle: `view_w` for even `orient`, `view_h` for odd. |
| `visH` | `int visH() const` | Height in world pixels of the shown world rectangle: `view_h` for even `orient`, `view_w` for odd. |
| `toScreen` | `Rect toScreen(Rect world) const` | Maps a world rect to a screen rect; swaps w and h for odd `orient`. Pass a 1x1 rect for a point. |
| `spriteFlags` | `SpriteFlags spriteFlags(SpriteFlags f) const` | Adds `orient` to the rotation field modulo 4; FlipX and FlipY are kept. |
| `clamp` | `void clamp();` | `clampAxis` on both axes, with `visW()` / `visH()` as the view size against `world_w` / `world_h`. |
| `moveTo` | `void moveTo(int nx, int ny);` | sets x, y, then clamps |
| `centerOn` | `void centerOn(int px, int py);` | puts the world point (px, py) at the centre of the shown rectangle (`visW()` x `visH()`), then clamps. |

### Free functions and types

| Name | Signature | Meaning |
|---|---|---|
| `clampAxis` | `int clampAxis(int pos, int view, int world);` | Camera position on one axis for a view of `view` pixels in a world of `world` pixels. If the world is larger than the view, pos is clamped to `[0, world - view]`. Otherwise the world is centred: `floor((world - view) / 2)`, which is <= 0 (the world is drawn with a margin; floor keeps odd differences deterministic). |
| `parallaxOffset` | `int parallaxOffset(int cam, int factor_q8);` | `floor(cam * factor_q8 / 256)` for a Q8 factor: 256 = 1.0 (moves with the world), 0 = fixed background, 128 = half speed, > 256 = foreground that moves faster. Floors for negative cam too. |
| `orientForGravity` | `inline uint8_t orientForGravity(GravityDir g)` | Orientation that puts gravity `g` at the bottom of the screen: Down 0, Right 1, Up 2, Left 3. `GravityDir` is in `pge/collision.h`. |
| `MapLayer` | `struct MapLayer { TileLayer tiles; uint16_t px_q8 = 256, py_q8 = 256; };` | One tile layer with its parallax factors (Q8, see `parallaxOffset`). |
| `drawLayers` | `void drawLayers(Canvas& c, const Camera& cam, const MapLayer* layers, int count);` | Draws `layers[0]` (back) to `layers[count - 1]` (front); a layer drawn over another needs a transparent tileset. Null `layers` or `count <= 0` draws nothing. The caller keeps `count <= PGE_PARALLAX_LAYERS` (not enforced). Each layer is drawn with `cam.orient`; parallax offsets are computed per world axis from `cam.x` and `cam.y`, as before. |

### Turning the view (orientation)

- `orient` is the number of clockwise quarter turns of the world picture: 0 as drawn, 1 turned 90 degrees clockwise, 2 turned 180, 3 turned 270. It uses the same numbers as the rotation field of `SpriteFlags` (`Rot90`, `Rot180`, `Rot270`).
- Camera `x`, `y` always name the top-left corner of the world rectangle the screen shows, in world pixels, for every `orient`. The turn happens around that rectangle; `visW()` and `visH()` give its size.
- Corner rule: the turn is clockwise, so each world corner moves clockwise with the picture. At `orient` 1 the world's top-left corner appears at the screen's top-right corner.
- To draw consistently, convert world rects with `cam.toScreen(rect)` for the screen position and size, and pass `cam.spriteFlags(flags)` to `Canvas::sprite`. For a tile map, call `Canvas::tilemap(layer, cam.x, cam.y, cam.orient)`; `drawLayers` does this for every layer.
- Gravity: `orientForGravity(g)` gives the `orient` that puts gravity at the bottom of the screen: Down 0, Right 1, Up 2, Left 3. These are orient numbers, not the values of the `GravityDir` enum.
- The F429 flush transpose only maps the 320x240 picture onto the portrait panel. It is unrelated to `orient` and does not rotate the game view.
- No new flags are needed: `SpriteFlags` already cover all 8 orientations. `Rot90` / `Rot180` / `Rot270` combined with `FlipX` / `FlipY` give all eight; a plain transpose is `Rot90` or `Rot270` with one flip.

## `pge/ui.h`

Immediate-mode widgets in `pge::ui`: pure functions of their arguments, drawn from onRender (every band redraws them), no state. Includes `pge/engine.h` and `pge/gfx.h`.

| Name | Signature | Meaning |
|---|---|---|
| `Style` | `struct Style { const Font* font; Color fg, bg, accent; uint8_t pad; };` | |
| `formatU32` | `int formatU32(char* buf, uint8_t cap, uint32_t v);` | Decimal, NUL-terminated; returns the digit count, or 0 (and `buf[0] = 0` if `cap > 0`) when buf / cap is too small. |
| `label` | `void label(Canvas& c, const Style& s, int x, int y, const char* text);` | fg text, transparent background |
| `bar` | `void bar(Canvas& c, const Style& s, Rect r, uint16_t value, uint16_t max, Color fill);` | 1 px fg outline, bg interior, `fill` for value/max of the interior width (value clamped to max; max 0 = empty). |
| `iconGrid` | `void iconGrid(Canvas& c, const Style& s, Rect area, const SpriteSheet& icons, const uint16_t* frames, const uint8_t* counts, uint8_t cols, uint8_t rows, int8_t selected);` | cols x rows cells of (icon + 2*pad) size; `frames[i] == 0xFFFF` is an empty slot, `counts[i] > 1` (counts may be null) is printed bottom-right; `selected < 0` selects nothing. |
| `listMenu` | `void listMenu(Canvas& c, const Style& s, Rect r, const char* const* items, uint8_t n, uint8_t selected);` | Rows of `font->h + 2*pad`; the list scrolls so that `selected` is the last visible row when it does not fit. |
| `MenuNav` | `struct MenuNav { uint8_t selected = 0; int update(const Input& in, uint8_t n); };` | `update`: Up / Down wrap; returns the activated index (A or Start), else -1. |
| `kNoIcon`, `Toast`, `ToastText` | `constexpr uint16_t kNoIcon = 0xFFFF; struct Toast { uint16_t text; uint16_t icon; uint8_t priority; uint16_t ticks; }; using ToastText = const char* (*)(const void* ctx, uint16_t text);` | `text` is an id that the game maps to a string at draw time (`ToastText`, may return null); `icon` is a sprite frame or `kNoIcon`; `ticks` are left once visible. |
| `ToastQueue` | `class ToastQueue { static constexpr uint8_t kCap = 8; uint8_t max_visible = 3; bool push(uint16_t text, uint16_t icon, uint8_t priority, uint16_t ticks); void tick(); uint8_t count() const; uint8_t visibleCount() const; const Toast& at(uint8_t i) const; const Toast* find(uint16_t text) const; uint16_t dropped() const; void clear(); };` | Notification queue in fixed storage. Holds state (like `MenuNav`): `tick()` belongs in onTick, `toasts()` in onRender. It never reads Input, so toasts never block input. |
| `toasts` | `void toasts(Canvas& c, const Style& s, int x, int y_bottom, int w, const ToastQueue& q, const SpriteSheet* icons, ToastText text, const void* ctx);` | Draws only the visible toasts, stacked upward: row height = max(font h, icon h) + 2*pad, the oldest is the bottom row and ends at `y_bottom`. Each row: bg, 1 px accent outline, icon (if `icons` and the toast has one), then fg text. A pure function of its arguments, like the other widgets. |

Toast rules:

- Entries `[0, visibleCount())` are visible, oldest first (`visibleCount() = min(count(), max_visible)`); the rest wait, highest priority first, first come first served within a priority. Only visible toasts count down; one that expires gives its slot to the first waiting one, whose countdown starts then. `push` with `ticks == 0` returns false and changes nothing.
- A toast pushed with `ticks = N` into a free slot is still queued after N - 1 `tick()` calls and gone after N.
- A push with a text id that is already queued (visible or waiting) updates that toast instead of adding a row: new icon, the timer restarts, priority is the higher of the two (a waiting one is re-sorted). A message raised every tick therefore does not flood the queue.
- A full queue (`kCap` = 8) drops the new toast, or the last waiting one if its priority is lower than the new one's. Visible toasts are never displaced. Every drop adds to `dropped()` (saturating); `clear()` empties the queue and keeps that counter.

```cpp
// onTick: a toast per Death event; the queue merges repeats of the same text id
Event ev;
while (ent_.popEvent(ev))
  if (ev.type == EventType::Death) toasts_.push(kTxtDied, kIconSkull, 1, 120);
toasts_.tick();
// onRender (last, so the stack sits on top): up to 3 rows at the bottom left
ui::toasts(c, style, 4, PGE_LOGICAL_H - 4, 112, toasts_, &icons_, textOf, &strings_);
```

## `pge/particles.h`

Pooled particles: compact 8 B records, no collision, no handles. Deviates from the S5.2 plan wording 'pooled entity kind': the 8 B record of 03 section 3.2 keeps them cheap. Includes `pge/config.h`, `pge/gfx.h`, `pge/memory.h`.

### `struct Particle`

Positions in world pixels, velocities in integer px/tick. `static_assert(sizeof(Particle) == 8)`.

| Field | Type | Meaning |
|---|---|---|
| `x`, `y` | `int16_t` | |
| `vx`, `vy` | `int8_t` | |
| `life` | `uint8_t` | `update()` calls left |
| `color` | `uint8_t` | palette index |

### `class Particles`

Backed by `Pool<Particle, PGE_PARTICLE_COUNT>`.

| Member | Signature | Meaning |
|---|---|---|
| `spawn` | `bool spawn(int x, int y, int vx, int vy, uint8_t life, uint8_t color);` | Clamps x, y to int16 and vx, vy to int8. False when the pool is full (`counters().full` + 1) or `life == 0`. |
| `update` | `void update(uint8_t gravity_period = 0);` | Gravity acts before the position step: every update with `life % gravity_period == 0` adds 1 to vy (max 127). Positions saturate at the int16 range. A particle is freed by the update that takes its life to 0. |
| `draw` | `void draw(Canvas& c, int cam_x, int cam_y, const Color* palette, uint8_t palette_size) const;` | One pixel per live particle; `color >= palette_size` falls back to `palette[0]`. Does not change state. |
| `size` | `uint16_t size() const` | |
| `counters` | `const PoolCounters& counters() const` | for `Engine::trackPool` |
