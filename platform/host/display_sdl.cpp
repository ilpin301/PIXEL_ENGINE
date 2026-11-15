// Host display HAL on SDL2: streaming RGB565 texture, window 2x, nearest scaling.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "pge/hal/display.h"
#include "host_bus.h"

static SDL_Window* g_win;
static SDL_Renderer* g_ren;
static SDL_Texture* g_tex;
static int g_quit;
static int g_w, g_h;

extern "C" {

pge_error pge_hal_display_init(uint16_t want_w, uint16_t want_h, pge_display_info* out) {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) return -1;
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
  g_win = SDL_CreateWindow("pge proto", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, want_w * 2, want_h * 2, 0);
  if (!g_win) return -1;
  g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_PRESENTVSYNC);
  if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, 0);  // dummy / software drivers have no vsync
  if (!g_ren || SDL_RenderSetLogicalSize(g_ren, want_w, want_h) != 0) return -1;
  g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, want_w, want_h);
  if (!g_tex) return -1;
  g_w = want_w;
  g_h = want_h;
  if (out) { out->panel_w = want_w; out->panel_h = want_h; out->flags = 0; }
  return 0;
}

pge_error pge_hal_display_flush(pge_rect r, uint16_t* px) {
  SDL_Rect sr = {r.x, r.y, r.w, r.h};
  pge_host_bus_charge(r.w * r.h * 2);
  return SDL_UpdateTexture(g_tex, &sr, px, r.w * 2) == 0 ? 0 : -1;
}

pge_error pge_hal_display_wait(void) { return 0; }
void pge_hal_display_frame_begin(void) {}

int pge_host_pump(void) {
  SDL_Event e;
  while (SDL_PollEvent(&e))
    if (e.type == SDL_QUIT) g_quit = 1;
  return g_quit;
}

void pge_hal_display_frame_end(void) {
  SDL_RenderClear(g_ren);
  SDL_RenderCopy(g_ren, g_tex, nullptr, nullptr);
  SDL_RenderPresent(g_ren);
  pge_host_pump();
}

int pge_host_quit_requested(void) { return g_quit; }

// window pixel -> display pixel (letterbox-aware), clamped to the display; nonzero if there is no renderer
int pge_host_window_to_display(int wx, int wy, int* dx, int* dy) {
  if (!g_ren || g_w <= 0 || g_h <= 0) return -1;
  float lx, ly;
  SDL_RenderWindowToLogical(g_ren, wx, wy, &lx, &ly);
  int x = (int)lx, y = (int)ly;
  *dx = x < 0 ? 0 : x >= g_w ? g_w - 1 : x;
  *dy = y < 0 ? 0 : y >= g_h ? g_h - 1 : y;
  return 0;
}

int pge_host_save_bmp(const char* path) {
  int w, h;
  SDL_GetRendererOutputSize(g_ren, &w, &h);
  SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!s) return -1;
  // redraw instead of reading the presented back buffer (undefined after present)
  SDL_RenderClear(g_ren);
  SDL_RenderCopy(g_ren, g_tex, nullptr, nullptr);
  int err = SDL_RenderReadPixels(g_ren, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
  if (err == 0) err = SDL_SaveBMP(s, path);
  SDL_FreeSurface(s);
  return err == 0 ? 0 : -1;
}

}  // extern "C"
