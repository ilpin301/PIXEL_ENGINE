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

static const char* const kToastStr[2] = {"A", "B"};
static const char* toastText(const void* ctx, uint16_t id) { return id < 2 ? static_cast<const char* const*>(ctx)[id] : nullptr; }
static ui::ToastQueue g_toasts;
static void sceneToast(Canvas& c) { ui::toasts(c, kStyle, 6, 60, 50, g_toasts, &kIcons, toastText, kToastStr); }

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

  // ---- ToastQueue ----
  {
    ui::ToastQueue q;
    CHECK(q.max_visible == 3 && q.count() == 0 && q.visibleCount() == 0 && q.dropped() == 0 && !q.find(1));
    q.tick();  // empty: nothing happens
    CHECK(q.push(1, ui::kNoIcon, 1, 3));  // timeout is exact: still there after 2 ticks, gone after 3
    q.tick();
    q.tick();
    CHECK(q.count() == 1 && q.find(1) && q.find(1)->ticks == 1);
    q.tick();
    CHECK(q.count() == 0 && !q.find(1));
  }
  {
    ui::ToastQueue q;  // priority order with one visible slot
    q.max_visible = 1;
    CHECK(q.push(10, 0, 1, 5) && q.push(11, 0, 1, 5) && q.push(12, 0, 5, 5) && q.push(13, 0, 1, 5));
    CHECK(q.count() == 4 && q.visibleCount() == 1);
    CHECK(q.at(0).text == 10 && q.at(1).text == 12 && q.at(2).text == 11 && q.at(3).text == 13);
    q.tick();
    CHECK(q.at(0).ticks == 4 && q.at(1).ticks == 5 && q.at(2).ticks == 5 && q.at(3).ticks == 5);  // waiting ones do not count down
    for (int i = 0; i < 4; i++) q.tick();  // 10 expires on the 5th tick
    CHECK(q.count() == 3 && q.at(0).text == 12 && q.at(0).ticks == 5);  // 12 moved up, its countdown starts now
    q.tick();
    CHECK(q.at(0).ticks == 4 && q.at(1).text == 11 && q.at(1).ticks == 5);
  }
  {
    ui::ToastQueue q;  // stacking: three visible, a fourth waits even with a top priority
    for (uint16_t i = 0; i < 3; i++) CHECK(q.push(i, ui::kNoIcon, 1, 10));
    CHECK(q.count() == 3 && q.visibleCount() == 3 && q.at(0).text == 0 && q.at(2).text == 2);
    CHECK(q.push(3, ui::kNoIcon, 9, 10) && q.count() == 4 && q.visibleCount() == 3 && q.at(3).text == 3);
    ui::ToastQueue u;  // an expired visible toast gives its slot to the first waiting one
    u.max_visible = 2;
    CHECK(u.push(1, 0, 1, 2) && u.push(2, 0, 1, 10) && u.push(3, 0, 1, 10));
    u.tick();
    u.tick();
    CHECK(u.count() == 2 && u.at(0).text == 2 && u.at(1).text == 3 && u.at(0).ticks == 8 && u.at(1).ticks == 10);
  }
  {
    ui::ToastQueue q;  // merge: same text id, no new row
    CHECK(q.push(7, 0, 1, 10));
    q.tick();
    q.tick();
    CHECK(q.push(7, 1, 3, 20) && q.count() == 1);
    CHECK(q.find(7)->ticks == 20 && q.find(7)->icon == 1 && q.find(7)->priority == 3);
    CHECK(q.push(7, 0, 1, 5) && q.count() == 1 && q.find(7)->priority == 3 && q.find(7)->ticks == 5);
    ui::ToastQueue w;  // a waiting toast moves forward with a higher priority, behind the equal ones
    w.max_visible = 1;
    CHECK(w.push(1, 0, 1, 9) && w.push(2, 0, 1, 9) && w.push(3, 0, 1, 9) && w.push(4, 0, 2, 9));
    CHECK(w.at(0).text == 1 && w.at(1).text == 4 && w.at(2).text == 2 && w.at(3).text == 3);
    CHECK(w.push(3, 0, 5, 9) && w.count() == 4);
    CHECK(w.at(0).text == 1 && w.at(1).text == 3 && w.at(2).text == 4 && w.at(3).text == 2);
    CHECK(w.push(2, 0, 5, 9) && w.count() == 4);
    CHECK(w.at(0).text == 1 && w.at(1).text == 3 && w.at(2).text == 2 && w.at(3).text == 4);
    CHECK(w.push(1, 0, 9, 9) && w.at(0).text == 1 && w.at(0).priority == 9);  // a visible one stays in place
  }
  {
    ui::ToastQueue ma;  // waiting merge, same priority: keeps its place in line, icon and ticks update
    ma.max_visible = 1;
    CHECK(ma.push(1, 0, 1, 9) && ma.push(2, 0, 1, 9) && ma.push(3, 0, 1, 9));
    CHECK(ma.push(2, 4, 1, 5) && ma.count() == 3);
    CHECK(ma.at(0).text == 1 && ma.at(1).text == 2 && ma.at(2).text == 3);
    CHECK(ma.at(1).icon == 4 && ma.at(1).ticks == 5 && ma.at(1).priority == 1);
    ui::ToastQueue mb;  // waiting merge, lower new priority: keeps the old priority and its place
    mb.max_visible = 1;
    CHECK(mb.push(1, 0, 1, 9) && mb.push(2, 0, 3, 9) && mb.push(3, 0, 3, 9) && mb.push(4, 0, 2, 9));
    CHECK(mb.push(2, 4, 1, 5) && mb.count() == 4);
    CHECK(mb.at(0).text == 1 && mb.at(1).text == 2 && mb.at(2).text == 3 && mb.at(3).text == 4);
    CHECK(mb.at(1).priority == 3 && mb.at(1).icon == 4 && mb.at(1).ticks == 5);
  }
  {
    ui::ToastQueue q;  // max_visible 7: exactly one waiting slot, a higher priority displaces it
    q.max_visible = 7;
    for (uint16_t i = 0; i < 8; i++) CHECK(q.push(i, 0, 2, 9));
    CHECK(q.visibleCount() == 7 && q.count() == 8 && q.at(7).text == 7);
    CHECK(!q.push(20, 0, 2, 9) && q.dropped() == 1 && q.at(7).text == 7);  // equal: refused
    CHECK(q.push(21, 0, 3, 9) && q.dropped() == 2 && q.count() == 8);
    CHECK(!q.find(7) && q.at(7).text == 21 && q.at(6).text == 6 && q.visibleCount() == 7);
    ui::ToastQueue fm;  // a waiting merge on a full queue needs no room: no drop, nothing displaced
    fm.max_visible = 1;
    for (uint16_t i = 0; i < 8; i++) CHECK(fm.push(i, 0, 2, 9));
    CHECK(fm.push(7, 3, 2, 4) && fm.count() == 8 && fm.dropped() == 0 && fm.at(7).text == 7 && fm.at(7).icon == 3);
    CHECK(fm.push(7, 3, 5, 4) && fm.count() == 8 && fm.dropped() == 0 && fm.at(1).text == 7 && fm.at(1).priority == 5);
    CHECK(fm.find(1) && fm.find(6) && fm.at(2).text == 1);
  }
  {
    ui::ToastQueue q;  // full queue: one visible slot, seven waiting
    q.max_visible = 1;
    for (uint16_t i = 0; i < 8; i++) CHECK(q.push(i, 0, 2, 9));
    CHECK(q.count() == 8 && q.dropped() == 0);
    CHECK(!q.push(20, 0, 1, 9) && q.dropped() == 1 && q.count() == 8 && !q.find(20));  // lower priority: refused
    CHECK(!q.push(21, 0, 2, 9) && q.dropped() == 2 && q.count() == 8 && !q.find(21));  // equal: refused
    CHECK(q.push(0, 0, 2, 4) && q.dropped() == 2 && q.count() == 8 && q.find(0)->ticks == 4);  // a merge needs no room
    CHECK(q.push(22, 0, 3, 9) && q.dropped() == 3 && q.count() == 8);  // higher: the last waiting one (7) is dropped
    CHECK(!q.find(7) && q.find(6) && q.at(0).text == 0 && q.at(1).text == 22);
    q.clear();
    CHECK(q.count() == 0 && q.visibleCount() == 0 && !q.find(0) && q.dropped() == 3);  // clear keeps the counter
    ui::ToastQueue v;  // visible toasts are never displaced
    v.max_visible = 8;
    for (uint16_t i = 0; i < 8; i++) CHECK(v.push(i, 0, 1, 9));
    CHECK(!v.push(30, 0, 9, 9) && v.dropped() == 1 && !v.find(30) && v.at(7).text == 7);
    for (int i = 0; i < 70000; i++) v.push(31, 0, 1, 9);
    CHECK(v.dropped() == 65535);  // saturates
  }
  {
    ui::ToastQueue q;  // ticks 0: refused, not counted, an existing toast is not touched
    CHECK(!q.push(1, 0, 1, 0) && q.count() == 0 && q.dropped() == 0);
    CHECK(q.push(1, 0, 1, 7) && !q.push(1, 2, 9, 0) && q.count() == 1 && q.find(1)->ticks == 7 && q.find(1)->priority == 1);
    ui::ToastQueue z;  // max_visible 0: everything waits, tick() changes nothing
    z.max_visible = 0;
    CHECK(z.push(1, 0, 1, 3) && z.push(2, 0, 2, 3) && z.count() == 2 && z.visibleCount() == 0);
    CHECK(z.at(0).text == 2);
    z.tick();
    z.tick();
    z.tick();
    CHECK(z.count() == 2 && z.at(0).ticks == 3 && z.at(1).ticks == 3);
  }
  {
    // pixels: 8 x 8 icons, pad 1, 7 px font -> 10 px rows; x = 6, bottom edge 60, width 50; two visible, a third waits
    g_toasts.clear();
    g_toasts.max_visible = 2;
    CHECK(g_toasts.push(0, 0, 1, 100) && g_toasts.push(1, ui::kNoIcon, 1, 100) && g_toasts.push(5, 0, 1, 100));
    draw(sceneToast, g_buf);
    CHECK(g_buf[59 * W + 6] == kAcc && g_buf[59 * W + 55] == kAcc && g_buf[50 * W + 6] == kAcc);  // bottom row outline
    CHECK(g_buf[40 * W + 6] == kAcc && g_buf[49 * W + 6] == kAcc && g_buf[39 * W + 6] == kBG);    // exactly one row higher
    CHECK(g_buf[60 * W + 6] == kBG && g_buf[30 * W + 6] == kBG);                                    // the waiting one is not drawn
    CHECK(g_buf[55 * W + 40] == kPanel && g_buf[45 * W + 40] == kPanel);
    CHECK(count(g_buf, Rect{8, 51, 8, 8}, kRed) == 64 && count(g_buf, Rect{8, 51, 8, 8}, kFG) == 0);  // bottom row: icon, ...
    CHECK(count(g_buf, Rect{17, 51, 30, 7}, kFG) > 0);                                                // ... then the text after it
    CHECK(count(g_buf, Rect{6, 40, 50, 10}, kRed) == 0 && count(g_buf, Rect{8, 41, 20, 7}, kFG) > 0);  // upper row: no icon, text first
    CHECK(count(g_buf, Rect{0, 0, W, H}, kBG) == W * H - 2 * 50 * 10);
    Canvas c(g_buf, W, H, 0, H, 1);  // no icons, no text function: 9 px rows of plain boxes
    c.clear(kBG);
    ui::toasts(c, kStyle, 6, 60, 50, g_toasts, nullptr, nullptr, nullptr);
    CHECK(g_buf[59 * W + 6] == kAcc && g_buf[42 * W + 6] == kAcc && g_buf[41 * W + 6] == kBG);
    CHECK(count(g_buf, Rect{0, 0, W, H}, kFG) == 0 && count(g_buf, Rect{0, 0, W, H}, kRed) == 0);
    c.clear(kBG);
    ui::ToastQueue none;
    ui::toasts(c, kStyle, 6, 60, 50, none, &kIcons, toastText, kToastStr);
    CHECK(count(g_buf, Rect{0, 0, W, H}, kBG) == W * H);
  }

  // ---- band independence (one band vs 16-line and 7-line bands) ----
  {
    static const Scene scenes[] = {sceneBar, sceneLabel, sceneList, sceneIcons, sceneOverlay, sceneToast};
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
