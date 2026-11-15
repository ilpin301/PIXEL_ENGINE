# 02 HAL, renderer and game API (session S2.2, 2026-11-15)
API spec - not implementation. Listings show the contract; names and signatures may be refined in S4 and any change is logged in `STATUS.md`. Tags: [verified] source named; [calculated] formula shown; [assumed] design judgement.

## Summary for later sessions
- HAL = 6 C headers: display, time, input, audio, storage, debug. Link-time bound (K7). Errors are `pge_error` (0 = ok).
- Display coordinates = the plane the core renders into, after integer scale and window offset. The platform maps them to the panel and may transpose (R10 proposal D7: DISCO panel is portrait 240x320, core stays landscape).
- Flush contract: one outstanding flush; caller owns the pixel buffer until `pge_hal_display_wait()` returns or the flush call returned with a sync platform; buffer content is undefined afterwards (the platform may byte-swap in place). Present path: with PGE_DISP_VSYNC, flush writes the back buffer and `frame_end` swaps and blocks until the swap is latched (LTDC reload on the next vsync); without it bands go straight into the scanned-out buffer and tearing is accepted (R2).
- Pixels are host-endian RGB565 (`Color`). Byte swap is done by the platform at flush (profile flag byte_swap_on_flush).
- Renderer: the engine runs `onRender(Canvas&)` once per band under a clip rectangle. All Canvas coordinates are logical-screen coordinates; the canvas clips to the band.
- Render-callback contract: runs once per band per frame; must not change game state; any randomness is seeded from `canvas.frameSeed()` = hash(tick count, constant `PGE_GAME_SEED`), never wall-clock, so renders are reproducible; the renderer clears each band to the clear colour before the call; no allocation; no engine lifecycle calls.
- Game API: no virtual base class. `pge::run(game)` calls `onCreate(Engine&)`, `onTick(const Input&)` at a fixed rate (K4), `onRender(Canvas&)`, optional `onDestroy()`. Input is a parameter, not a global.
- Spec sample game is expressible with this API (section 4.3, differences listed with reasons).
- Error policy: init and I/O return `Error`; draw calls return void, clip silently and count dropped calls in Debug.
- Cost: every draw call in `onRender` runs once per band, so total render CPU = per-band cost x number of bands (R1). Measured in S3.1a.
- Scale staging (K15) needs its own buffer: (N x s) lines x (W x s) pixels x 2 B [calculated]; example N = 8, s = 2, W = 160: 16 x 320 x 2 = 10,240 B. 01 section 5.3 now lists `scale_staging_bytes` per profile (excluded from totals); S4.2a wires it into the profile.
- Quit: `Engine::requestQuit()` and the input bit `PGE_INPUT_QUIT` (host sets it on window close); `run<G>` then returns Ok.
- Decisions proposed here: D7-D12 (section 7). Open for S2.3: world, entity, storage, resource, audio, UI APIs and the R13 split.

## 1 Conventions
- C-compatible HAL: `pge_hal_<module>_<verb>`, types `pge_*`. C++ engine API in namespace `pge`, PascalCase types, lowerCamel functions (01 section 7).
- `Color` = `uint16_t` RGB565. `Fix` = `int32_t` 16.16 fixed point; helpers `fix(int)`, `toInt(Fix)`. No `float` in the core API (K4).
- "ISR-safe" = may be called from interrupt context. Unless stated, no HAL function is ISR-safe.
- "Blocking" = may wait for hardware. Every blocking call states its worst-case wait source.

## 2 HAL
### 2.1 Headers
```c
/* API spec - not implementation. pge/hal/*.h */
typedef int pge_error;                      /* 0 = ok, see section 5 */
typedef struct { uint16_t x, y, w, h; } pge_rect;

/* display.h */
#define PGE_DISP_ASYNC      0x01  /* flush may return before the transfer ends */
#define PGE_DISP_TRANSPOSED 0x02  /* platform rotates internally (panel is portrait) */
#define PGE_DISP_VSYNC      0x04  /* two scan-out buffers: flush -> back buffer, frame_end swaps and blocks until the reload is latched (LTDC: next vsync) */
typedef struct { uint16_t panel_w, panel_h; uint32_t flags; } pge_display_info;
pge_error pge_hal_display_init(uint16_t want_w, uint16_t want_h, pge_display_info* out);
pge_error pge_hal_display_flush(pge_rect r, uint16_t* px);   /* px: r.w*r.h RGB565, row-major, stride r.w */
pge_error pge_hal_display_wait(void);                        /* returns when the last flush ended */
void      pge_hal_display_frame_begin(void);                 /* hint, may be empty */
void      pge_hal_display_frame_end(void);                   /* VSYNC set: buffer swap; else hint */

/* time.h */
uint32_t  pge_hal_time_us(void);                             /* monotonic, wraps at 2^32 us (71.6 min) */
void      pge_hal_time_delay_us(uint32_t us);

/* input.h */
#define PGE_INPUT_QUIT 0x80000000u                           /* bit in buttons; host sets on window close */
typedef struct { uint32_t buttons; int16_t axis[4]; uint8_t touch_down; int16_t touch_x, touch_y; } pge_raw_input;
pge_error pge_hal_input_init(void);
pge_error pge_hal_input_poll(pge_raw_input* out);

/* audio.h */
typedef void (*pge_audio_fill_fn)(int16_t* stereo, uint32_t frames);
pge_error pge_hal_audio_init(uint32_t sample_rate, pge_audio_fill_fn fill);
pge_error pge_hal_audio_start(void);
pge_error pge_hal_audio_stop(void);

/* storage.h */
typedef int32_t pge_file;                                    /* < 0 = invalid */
pge_error pge_hal_storage_mount(void);
pge_error pge_hal_storage_open(const char* path, int mode, pge_file* out);  /* mode: 0 read, 1 write-truncate */
int32_t   pge_hal_storage_read(pge_file f, void* buf, uint32_t n);          /* bytes read, < 0 = error */
int32_t   pge_hal_storage_write(pge_file f, const void* buf, uint32_t n);
pge_error pge_hal_storage_close(pge_file f);
pge_error pge_hal_storage_rename(const char* from, const char* to);         /* `to` must not exist; not atomic-replace */
pge_error pge_hal_storage_remove(const char* path);

/* debug.h */
void pge_hal_debug_write(const char* s, uint32_t n);         /* best effort, may drop */
void pge_hal_fatal(const char* msg);                         /* does not return */
```
### 2.2 Contracts
| Function | Blocking | ISR-safe | Buffer owner | Notes |
|---|---|---|---|---|
| display_init | yes, panel reset and init sequences | no | - | `want_w/h` = display coordinates the core will use; returns NotSupported if the panel cannot show them even with transposing |
| display_flush | sync platform: until the transfer ends; async platform: until the previous flush ended, then returns | no | caller until wait() returns (sync: until flush returns) | one outstanding flush; content undefined after; rect lies inside display coordinates, else InvalidArg |
| display_wait | yes, up to one flush duration | no | returns ownership | no-op on sync platforms |
| frame_begin / frame_end | PGE_DISP_VSYNC set: frame_end blocks until the buffer swap / reload is latched; else hint, may be empty | no | - | VSYNC set: frame_end swaps / reloads the back buffer, so frame N+1 never draws into the buffer still being scanned out; else hint, may be empty |
| time_us | no | yes | - | core uses unsigned subtraction for wrap |
| time_delay_us | yes | no | - | frame pacing only; may be coarse |
| input_poll | no (short, bounded: ADC conversions measured ~25 us each on DISCO ref [verified, 01]) | no | caller | platform does debounce, stick centre and deadband; axes -32768..32767 |
| audio_init / start / stop | yes | no | - | fill callback is called from the platform's audio context (task or interrupt); mixer code in it must be bounded and allocation-free |
| storage_* | yes, device latency | no | caller | rename: target must not exist (callers `remove` first), platforms need not replace atomically, else Io; paths are platform-relative |
| debug_write | no | optional (platform states) | caller | may drop output |
| fatal | - | yes | - | stops the program, prints msg if possible |

Tradeoff: one outstanding flush keeps the HAL tiny; async double band buffers still work because the second buffer is drawn while the first is in flight.

## 3 Renderer
### 3.1 Types and calls
```cpp
// API spec - not implementation. pge/gfx.h, pge/sprite.h, pge/tilemap.h, pge/camera.h
namespace pge {
struct RenderConfig { uint16_t logical_w, logical_h; int16_t origin_x, origin_y; uint8_t scale; uint8_t band_lines; Color clear_color = 0; };
enum class SpriteFlags : uint8_t { None = 0, FlipX = 1, FlipY = 2, Rot90 = 4, Rot180 = 8, Rot270 = 12 };
class Canvas {                              // valid only inside onRender
 public:
  int  width() const;  int height() const;  // logical size
  uint32_t frameSeed() const;               // same for every band of one frame
  bool visible(Rect r) const;               // intersects the current band: use to cull
  void clear(Color c);                      // optional: the renderer already cleared the band to clear_color
  void pixel(int x, int y, Color c);
  void line(int x0, int y0, int x1, int y1, Color c);
  void rect(Rect r, Color c);  void fillRect(Rect r, Color c);
  void circle(int cx, int cy, int r, Color c);  void fillCircle(int cx, int cy, int r, Color c);
  void sprite(const SpriteSheet& s, uint16_t frame, int x, int y, SpriteFlags f);
  void tilemap(const TileLayer& l, const Camera& cam, uint8_t layer_slot);
  void text(const Font& f, int x, int y, const char* s, Color c);
};
struct Camera { Fix x, y; uint16_t view_w, view_h; Rect bounds;
  void follow(Fix tx, Fix ty);              // centre on target, then clamp to bounds
  int xi() const;  int yi() const; };
struct TileSource { const uint8_t* (*chunkRow)(void* ctx, int tx, int ty, int* runLength); void* ctx; };  // chunked-world variant
struct TileLayer { const uint8_t* tiles; uint16_t w, h; const Tileset* set; uint16_t parallax_x, parallax_y;   // parallax 8.8: 256 = 1.0
                   const TileSource* source = nullptr; };   // exactly one of tiles / source is set
}
```
Layers: draw order is call order; a layer is one `tilemap` call with its own parallax factor. No layer objects are stored by the renderer.
Tile sources: a flat layer reads `tiles[ty * w + tx]`. A chunked layer (World, 03) sets `source`; `chunkRow` returns a pointer to the tiles starting at (tx, ty) and sets `*runLength` = tiles valid before the chunk edge, or nullptr for an unloaded chunk (drawn as void). It only reads; loading stays in `ensureLoaded` (render never changes state). Parallax layers always use flat arrays, never the chunked World. API spec only; implemented in S5.1a.

### 3.2 Band lifecycle
frame start: `display.frame_begin` -> for each band, top to bottom: set clip to band rows, clear the band to `clear_color`, call `onRender(canvas)`, expand for scale if s > 1, `display.flush(rect, buf)` (with two band buffers: draw the next band while the previous flush is in flight) -> `display.wait` at frame end -> `display.frame_end` (present: buffer swap when PGE_DISP_VSYNC, section 2.2). With band lines = logical height the loop runs once (full framebuffer mode, K2).

### 3.3 Render-callback contract
1. Runs once per band, so up to `logical_h / band_lines` times per frame.
2. Reads game state, never writes it. The same state must give the same pixels in every band. Enforcement: a host debug build checksums engine-owned world and entity state before band 1 and after the last band and asserts equality (game state may register ranges) [assumed].
3. Randomness comes from `canvas.frameSeed()` = hash(tick count, `PGE_GAME_SEED`), never wall-clock (reproducible renders, host tests); per-band seeds would tear effects at band edges.
4. No allocation, no blocking calls, no engine lifecycle or flush calls.
5. Cull with `canvas.visible(rect)` before heavy work: cost is multiplied by the band count (R1).
6. Coordinates are logical-screen coordinates; the canvas translates and clips to the current band.
7. Each band buffer is already cleared to `clear_color` (default 0) when `onRender` starts, so `clear()` is optional extra; the buffer is never left holding byte-swapped residue (D8).

### 3.4 Scale, window, orientation (K15, R10)
- Integer scale s in {1,2,3}: the logical screen is `display / s`; each band is expanded into a staging buffer at flush. Staging = (N x s) x (W x s) x 2 B [calculated]. This saves render CPU, not bus time (03).
- Window: `origin_x/origin_y` offset the logical screen inside the display; flush rects are shifted. A 160x120 logical screen drawn 1:1 in a window sends 4x less data than 320x240 [calculated, 03].
- Orientation (D7): the core always renders in the display orientation it asked for. If the panel is natively rotated (DISCO 240x320), the platform transposes inside `display_flush` and sets PGE_DISP_TRANSPOSED. Fallback: the game runs portrait with `want_w/h` = panel size. Cost of the transposing copy is measured in S3.2.
- Optional hooks: `setDirtyHint(bool (*fn)(Rect band))` skips a band when the game says nothing changed there (band-skip, S5.6); default null = every band flushed.

## 4 Game-facing API
### 4.1 Lifecycle
```cpp
// API spec - not implementation. pge/engine.h
namespace pge {
Error init();                                  // checks profile, inits HAL, arenas, renderer
template <class G> Error run(G& game);         // G supplies the callbacks below; static dispatch, no virtual; returns Ok after quit
class Engine { public: void requestQuit(); };  // also set when input poll reports PGE_INPUT_QUIT; run<G> finishes the tick, calls onDestroy, returns
// G: Error onCreate(Engine&);  void onTick(const Input&);  void onRender(Canvas&);  void onDestroy();  (onDestroy optional)
class Input { public:
  bool held(Button b) const;  bool pressed(Button b) const;  bool released(Button b) const;  // pressed/released: edge in this tick
  int16_t axis(Axis a) const;                  // -32768..32767
  bool touch(int& x, int& y) const; };
}
```
Tick rate is a compile-time constant (`PGE_TICK_HZ`, profile or user config). Overrun and catch-up policy: defined in S4.1.

### 4.2 Other game-facing calls (specified in S2.3)
Entity pools, collision queries, chunked world, audio play, storage, UI widgets, debug overlay.

### 4.3 Spec sample game in this API
```cpp
// API spec - not implementation. Illustrative.
#include <pge/pge.h>
using namespace pge;
extern const SpriteSheetData kPlayerData;      // generated from player.png by tools/ (S5.4)
extern const uint8_t kTiles[128 * 128];
extern const Tileset kTileset;                 // 16x16 tiles
class MyGame {
 public:
  Error onCreate(Engine&) {
    player_ = SpriteSheet(kPlayerData);
    layer_ = TileLayer{kTiles, 128, 128, &kTileset, 256, 256};
    cam_.bounds = Rect{0, 0, 128 * 16, 128 * 16};                 // = setWorldSize(128, 128)
    return Error::Ok;
  }
  void onTick(const Input& in) {
    if (in.held(Button::Left))  px_ -= fix(1);
    if (in.held(Button::Right)) px_ += fix(1);
    cam_.follow(px_, py_);
  }
  void onRender(Canvas& c) {
    c.clear(kBlack);                                              // optional: band already cleared
    c.tilemap(layer_, cam_, 0);                                   // = drawTilemap(world)
    c.sprite(player_, 0, toInt(px_) - cam_.xi(), toInt(py_) - cam_.yi(), SpriteFlags::None);
  }
 private:
  SpriteSheet player_; TileLayer layer_; Camera cam_; Fix px_ = fix(64), py_ = fix(64);
};
int main() { static MyGame g; if (init() != Error::Ok) return 1; return run(g) == Error::Ok ? 0 : 2; }
```
Differences from the spec sample, with reasons:
1. `onUpdate(float deltaTime)` -> `onTick(const Input&)`: fixed timestep, no float (K4).
2. Static `Input::pressed` -> `Input` parameter: no hidden global, testable; `held` is level, `pressed` is edge (the spec's `pressed` meant level).
3. `loadSpriteSheet("player", "player.bmp")` -> data generated at build time and wrapped by `SpriteSheet`: no string lookup or file parser on the device; storage-loaded assets come with the Resource Manager (S2.3).
4. `setWorldSize` -> tile layer size plus `Camera.bounds`: the chunked world arrives in S5.1a behind the same `tilemap` call via `TileLayer.source` (section 3.1).
5. `onRender(Renderer&)` -> `onRender(Canvas&)`: the canvas is valid for one band only and the callback runs once per band (section 3.3).
6. Virtual `Game` base -> template `run<G>`: no vtable, no inheritance; a virtual-base wrapper may exist outside `core/`.
7. `engine.start<MyGame>()` -> `init()` then `run(game)`: init failure is a return value.
8. Positions are `Fix`, not int/float: sub-pixel motion without an FPU.

## 5 Error-handling and init-check policy
- `Error` (C++) / `pge_error` (C): Ok = 0, InvalidArg, OutOfMemory (pool or arena exhausted), NotSupported (feature off in the profile or hardware), Io, Timeout, Busy, Corrupt, NotInitialized.
- Init and I/O functions return `Error`. `init()` runs the checks: profile static_asserts at compile time; at run time the HAL display info must fit `logical x scale` (+ window) and the band buffers; storage mount failure is reported, not fatal.
- Draw calls return void: off-screen or invalid arguments clip or drop silently and increment a Debug counter (`draw_dropped`). Reason: a draw call is hot, runs once per band, and must not branch on error returns.
- Pools return an invalid handle and increment `pool_full[kind]` when exhausted; they never allocate.
- `PGE_ASSERT` in debug builds writes via `pge_hal_debug_write` and calls `pge_hal_fatal`; compiled out in release. `PGE_CHECK` (always on) guards trust boundaries: asset headers, save files, HAL return values.
Tradeoff: silent clipping hides game bugs unless the counter is watched; the debug overlay (S5.6) shows it.

## 6 Per-call cost notes
All entries [assumed] until S3.1a measures them. "Pixels" = pixels inside the band and clip.

| Call | Cost class | Note |
|---|---|---|
| clear, fillRect | O(pixels written) | memory-bound |
| pixel | O(1) | |
| line | O(length inside clip) | |
| rect (outline) | O(perimeter inside clip) | |
| circle | O(radius), fillCircle O(area) | |
| sprite | O(sprite pixels inside clip) | per-pixel palette lookup; transparent skips |
| tilemap | O(visible tiles x tile pixels) | only tiles that touch the band |
| text | O(glyphs x glyph pixels) | |
| camera follow, visible | O(1) | |
| display_flush | O(pixels), bus-bound | ceilings in 02 and 03 |
| band clear | O(band pixels) | once per band before `onRender` (memory-bound) |
| scale expand | O(pixels x s x s) | CPU, not bus |
| callback overhead | O(1) x bands | R1: fixed work in `onRender` is paid per band |

## 7 Decisions and open items
| Id | Decision (proposal for G2) | Tradeoff |
|---|---|---|
| D7 | Core renders in the asked display orientation; the platform transposes (R10) | transposing copy costs CPU on DISCO; portrait fallback available |
| D8 | One outstanding flush; buffer content undefined afterwards | simple HAL; platform may swap bytes in place, so the renderer clears each band before `onRender` (section 3.3) |
| D9 | Canvas passed per band; no global renderer | the callback cannot cache canvas state across bands |
| D10 | Static-dispatch game template, `Input` as parameter | no virtual base; unusual for Arduino-style users |
| D11 | Draw calls silent-clip with counters; init and I/O return Error | game bugs are visible only through counters |
| D12 | Scale needs a staging buffer; profile gains `scale_staging_bytes` (01 section 5.3) | extra RAM at s > 1, outside the totals |
Open for S2.3: world, entity, storage, resource, audio, UI APIs; R13 split (health in S5.2, AI in example, crafting in S6.2 per G1). Open for S4.1: tick overrun policy. UNKNOWN carried: DMA reach on F429 (RM not read), TE pin on the user's module.
