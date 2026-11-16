// UI widgets, menu navigation and debug overlay: pixel checks on a directly built Canvas, plus band independence.
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "pge/debug.h"
#include "pge/hal/display.h"
#include "pge/ui.h"

#define CHECK(c)                                                          \
  do {                                                                    \
    if (!(c)) {                                                           \
      std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
      return 1;                                                           \
    }                                                                     \
  } while (0)

using namespace pge;

// renderFrame (in pge_core) references the display HAL; these are never called here.
extern "C" {
pge_error pge_hal_display_init(uint16_t, uint16_t, pge_display_info*) { return 0; }
pge_error pge_hal_display_flush(pge_rect, uint16_t*) { return 0; }
pge_error pge_hal_display_wait(void) { return 0; }
void pge_hal_display_frame_begin(void) {}
void pge_hal_display_frame_end(void) {}
}

enum { W = 128, H = 80 };
static const Color kBG = 0x1234, kFG = 0xFFFF, kPanel = 0x0000, kAcc = 0xFFE0, kFill = 0x07E0, kRed = 0xF800;
static const ui::Style kStyle{&kFont5x7, kFG, kPanel, kAcc, 1};

static Color g_buf[W * H];

static int count(const Color* b, Rect r, Color c) {
  int n = 0;
  for (int y = r.y; y < r.y + r.h; y++)
    for (int x = r.x; x < r.x + r.w; x++) n += b[y * W + x] == c;
  return n;
}

// Scene = draws one widget into a fresh canvas; used for both the pixel tests and the band comparison.
typedef void (*Scene)(Canvas&);

static void draw(Scene s, Color* buf) {
  Canvas c(buf, W, H, 0, H, 1);
  c.clear(kBG);
  s(c);
}

static const uint8_t kIconPx[3 * 64] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
static const Color kIconPal[2] = {0, kRed};
static const SpriteSheet kIcons{kIconPx, kIconPal, 8, 8, 3, true};
static const uint16_t kFrames[6] = {0, 0xFFFF, 1, 2, 0xFFFF, 0};
static const uint8_t kCounts[6] = {0, 0, 5, 0, 0, 1};
static const char* const kItems[] = {"ONE", "TWO", "THREE", "FOUR", "FIVE"};
static int g_sel = 0;
static Stats g_stats;
static uint8_t g_flags = kOverlayAll;

static void sceneBar(Canvas& c) {
  ui::bar(c, kStyle, Rect{4, 5, 61, 12}, 7, 20, kFill);
  ui::bar(c, kStyle, Rect{70, 13, 40, 9}, 1, 0, kFill);
}
static void sceneLabel(Canvas& c) { ui::label(c, kStyle, 10, 10, "Hi 42"); }
static void sceneList(Canvas& c) { ui::listMenu(c, kStyle, Rect{3, 3, 50, 40}, kItems, 5, static_cast<uint8_t>(g_sel)); }
static void sceneIcons(Canvas& c) { ui::iconGrid(c, kStyle, Rect{2, 3, 60, 40}, kIcons, kFrames, kCounts, 3, 2, static_cast<int8_t>(g_sel)); }
static void sceneOverlay(Canvas& c) { drawOverlay(c, kStyle, g_stats, g_flags); }

static Input mkInput(uint32_t prev, uint32_t cur) {
  Input in;
  pge_raw_input r = {};
  r.buttons = prev;
  in.update(r, 1);
  r.buttons = cur;
  in.update(r, 1);
  return in;
}
static uint32_t B(Button b) { return static_cast<uint32_t>(b); }

static Color g_full[W * H], g_out[W * H], g_band[W * 16];

// Same scene as one full-height canvas and as bands of `bh` lines (separate Canvas objects over a band buffer).
static bool bandsMatch(Scene s, int bh) {
  draw(s, g_full);
  for (int by = 0; by < H; by += bh) {
    const int n = by + bh <= H ? bh : H - by;
    Canvas c(g_band, W, H, by, n, 1);
    c.clear(kBG);
    s(c);
    std::memcpy(g_out + by * W, g_band, sizeof(Color) * W * n);
  }
  return std::memcmp(g_full, g_out, sizeof g_full) == 0;
}

int main() {
  // ---- formatU32 ----
  char b[12];
  CHECK(ui::formatU32(b, sizeof b, 0) == 1 && !std::strcmp(b, "0"));
  CHECK(ui::formatU32(b, sizeof b, 7) == 1 && !std::strcmp(b, "7"));
  CHECK(ui::formatU32(b, sizeof b, 4294967295u) == 10 && !std::strcmp(b, "4294967295"));
  b[0] = 'x';
  CHECK(ui::formatU32(b, 10, 4294967295u) == 0 && b[0] == 0);
  b[0] = 'x';
  CHECK(ui::formatU32(b, 0, 5) == 0 && b[0] == 'x');
  CHECK(ui::formatU32(nullptr, 5, 5) == 0);

  // ---- MenuNav ----
  {
    ui::MenuNav nav;
    CHECK(nav.update(mkInput(0, B(Button::Up)), 3) == -1 && nav.selected == 2);  // wraps up
    CHECK(nav.update(mkInput(0, B(Button::Up)), 3) == -1 && nav.selected == 1);
    CHECK(nav.update(mkInput(B(Button::Up), B(Button::Up)), 3) == -1 && nav.selected == 1);  // held: no edge
    nav.selected = 2;
    CHECK(nav.update(mkInput(0, B(Button::Down)), 3) == -1 && nav.selected == 0);  // wraps down
    CHECK(nav.update(mkInput(0, B(Button::A)), 3) == 0);
    CHECK(nav.update(mkInput(B(Button::A), B(Button::A)), 3) == -1);
    CHECK(nav.update(mkInput(0, B(Button::Start)), 3) == 0);
    CHECK(nav.update(mkInput(0, B(Button::Down) | B(Button::A)), 3) == 1 && nav.selected == 1);  // move, then activate
    nav.selected = 9;
    CHECK(nav.update(mkInput(0, 0), 3) == -1 && nav.selected == 2);  // clamp
    CHECK(nav.update(mkInput(0, B(Button::A)), 0) == -1 && nav.selected == 0);
  }

  // ---- bar ----
  {
    const Rect in{5, 6, 59, 10};  // interior of {4,5,61,12}; fill width is 59 * 7 / 20 = 20
    draw(sceneBar, g_buf);
    CHECK(g_buf[5 * W + 4] == kFG && g_buf[16 * W + 64] == kFG);
    CHECK(count(g_buf, in, kFill) == 20 * 10);
    CHECK(g_buf[6 * W + 24] == kFill && g_buf[6 * W + 25] == kPanel);
    CHECK(count(g_buf, Rect{70, 13, 40, 9}, kFill) == 0 && g_buf[13 * W + 70] == kFG && g_buf[14 * W + 71] == kPanel);
    for (int v : {0, 20, 30}) {
      Canvas c(g_buf, W, H, 0, H, 1);
      c.clear(kBG);
      ui::bar(c, kStyle, Rect{4, 5, 61, 12}, static_cast<uint16_t>(v), 20, kFill);
      CHECK(count(g_buf, in, kFill) == (v == 0 ? 0 : 59 * 10));
    }
    Canvas c(g_buf, W, H, 0, H, 1);
    c.clear(kBG);
    ui::bar(c, kStyle, Rect{4, 5, 2, 12}, 1, 2, kFill);
    CHECK(count(g_buf, Rect{0, 0, W, H}, kBG) == W * H);  // too small: nothing
  }

  // ---- label ----
  {
    draw(sceneLabel, g_buf);
    const int tw = textWidth(kFont5x7, "Hi 42");
    CHECK(count(g_buf, Rect{10, 10, tw, 7}, kFG) > 0);
    CHECK(count(g_buf, Rect{0, 0, W, H}, kFG) == count(g_buf, Rect{10, 10, tw, 7}, kFG));  // nothing outside the box
    ui::Style nofont{nullptr, kFG, kPanel, kAcc, 1};
    Canvas c(g_buf, W, H, 0, H, 1);
    c.clear(kBG);
    ui::label(c, nofont, 10, 10, "X");
    ui::label(c, kStyle, 10, 10, nullptr);
    CHECK(count(g_buf, Rect{0, 0, W, H}, kBG) == W * H);
  }

  // ---- listMenu: rows are 9 px (7 + 2*1), 40 px tall -> 4 rows fit ----
  {
    g_sel = 1;
    draw(sceneList, g_buf);
    CHECK(g_buf[12 * W + 3] == kAcc && g_buf[3 * W + 3] == kPanel);  // row 1 selected, row 0 plain
    CHECK(count(g_buf, Rect{3, 3, 50, 9}, kFG) > 0 && count(g_buf, Rect{3, 3, 50, 9}, kAcc) == 0);
    g_sel = 4;  // beyond fit: scrolls so item 4 is the last visible row (index 3 of the window)
    draw(sceneList, g_buf);
    CHECK(g_buf[30 * W + 3] == kAcc && count(g_buf, Rect{3, 3, 50, 27}, kAcc) == 0);
    CHECK(count(g_buf, Rect{3, 3, 50, 9}, kFG) > 0);  // first visible row is item 1
    Canvas c(g_buf, W, H, 0, H, 1);
    c.clear(kBG);
    ui::listMenu(c, kStyle, Rect{3, 3, 50, 40}, kItems, 0, 0);
    CHECK(count(g_buf, Rect{3, 3, 50, 40}, kPanel) == 50 * 40);
  }

  // ---- iconGrid: 8x8 icons, pad 1 -> 10x10 cells; area origin (2,3) ----
  {
    g_sel = -1;
    draw(sceneIcons, g_buf);
    CHECK(count(g_buf, Rect{2, 3, 30, 20}, kAcc) == 0);
    CHECK(count(g_buf, Rect{3, 4, 8, 8}, kRed) == 64);               // cell 0 sprite
    CHECK(count(g_buf, Rect{13, 4, 8, 8}, kRed) == 0);              // cell 1 is empty
    CHECK(g_buf[3 * W + 12] == kFG);                                 // cell 1 outline still drawn
    CHECK(count(g_buf, Rect{23, 4, 8, 8}, kFG) > 0);                // cell 2 has count 5
    CHECK(count(g_buf, Rect{23, 4, 8, 8}, kRed) < 64);
    CHECK(count(g_buf, Rect{23, 14, 8, 8}, kFG) == 0);              // cell 5: count 1 is not printed
    g_sel = 3;
    draw(sceneIcons, g_buf);
    CHECK(g_buf[14 * W + 3] == kAcc && g_buf[13 * W + 2] == kFG);   // cell 3 (row 1): inner accent, outer fg
    CHECK(count(g_buf, Rect{2, 3, 30, 10}, kAcc) == 0);
    Canvas c(g_buf, W, H, 0, H, 1);
    c.clear(kBG);
    ui::iconGrid(c, kStyle, Rect{2, 3, 30, 20}, kIcons, nullptr, kCounts, 3, 2, 0);
    ui::iconGrid(c, kStyle, Rect{2, 3, 30, 20}, kIcons, kFrames, kCounts, 0, 2, 0);
    CHECK(count(g_buf, Rect{0, 0, W, H}, kBG) == W * H);
  }

  // ---- drawOverlay ----
  {
    g_stats = Stats{};
    static Color ref[W * H];
    draw([](Canvas&) {}, ref);
    g_flags = 0;
    draw(sceneOverlay, g_buf);
    CHECK(std::memcmp(g_buf, ref, sizeof ref) == 0);
#if PGE_DEBUG_OVERLAY
    g_flags = kOverlayAll;
    draw(sceneOverlay, g_buf);
    CHECK(std::memcmp(g_buf, ref, sizeof ref) != 0 && g_buf[0] == kPanel && count(g_buf, Rect{0, 0, W, H}, kFG) > 0);
    static Color timing[W * H];
    std::memcpy(timing, g_buf, sizeof timing);
    g_flags = kOverlayPools;  // all pool counters are zero: no lines at all
    draw(sceneOverlay, g_buf);
    CHECK(std::memcmp(g_buf, ref, sizeof ref) == 0);
    g_stats.pool_high[2] = 3;
    g_flags = kOverlayAll;
    draw(sceneOverlay, g_buf);
    CHECK(std::memcmp(g_buf, timing, sizeof timing) != 0);
    g_flags = kOverlayTiming;
    draw(sceneOverlay, g_buf);
    CHECK(std::memcmp(g_buf, timing, sizeof timing) == 0);  // pools line appears only with the pools flag
    g_stats.fps = 60;
    g_stats.tick_us = 123;
    g_flags = kOverlayAll;
    draw(sceneOverlay, g_buf);
    CHECK(std::memcmp(g_buf, timing, sizeof timing) != 0);
#endif
  }

  // ---- band independence (one band vs 16-line and 7-line bands) ----
  {
    static const Scene scenes[] = {sceneBar, sceneLabel, sceneList, sceneIcons, sceneOverlay};
    g_stats.fps = 60;
    g_stats.tick_us = 1234;
    g_stats.pool_high[1] = 9;
    g_stats.pool_full[1] = 2;
    g_sel = 4;
    g_flags = kOverlayAll;
    for (Scene s : scenes) {
      CHECK(bandsMatch(s, 16));
      CHECK(bandsMatch(s, 7));
    }
    g_sel = 3;
    CHECK(bandsMatch(sceneIcons, 7) && bandsMatch(sceneList, 16));
  }

  std::printf("test_ui: all checks passed\n");
  return 0;
}
