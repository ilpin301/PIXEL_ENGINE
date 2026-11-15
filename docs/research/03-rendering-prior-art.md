# 03 Rendering strategies and prior art (session S1.3, 2026-11-15)

## Summary for later sessions

- K2 (band rendering, one path): CONFIRMED. Band height is the knob; N = full height gives a classic framebuffer.
- K3 (band format RGB565, indexed assets): CONFIRMED. ILI9341 and LTDC both take RGB565. Indexed 8-bit buffer deferred to G3.
- K12 (honest numbers): stands, with a new rule: every bus number states the datasheet limit and the clock actually used.
- K15 (integer scale): stands, but pixel doubling at flush does not reduce SPI bus time. Windowed output is the alternative for SPI panels.
- No change to K1, K4-K11, K13, K14.
- Key number: in-spec ILI9341 SPI (10 MHz) gives a ceiling of 8.1 fps at 320x240 RGB565 [calculated]. 40 MHz is 4x out of spec.
- SPI bus time, not render CPU, is the ceiling on ILI9341 SPI. A smaller logical screen drawn 1:1 into a window is 4x faster (160x120 in 320x240).
- Proposed amendments for S2: (a) band-skip when nothing in a band changed; (b) windowed flush; (c) async flush with double band buffer where DMA exists; (d) on DISCO, band to SRAM then DMA2D copy to the SDRAM framebuffer [assumed, S3 to measure].
- Profile flag proposed: byte_swap_on_flush for RGB565 on SPI / 8-bit buses.
- Memory: a 320x240 RGB565 band of 16 lines is 10,240 B; the full frame is 153,600 B (about 30% of ESP32-S3 internal SRAM).
- Open risks: R1 (per-band re-run cost, measure in S3.1a), R2 (tearing, S3.2).
- Not hardware-verified: TE pin on the user's SPI module, DISCO LTDC panel timings, non-ILI9341 panel limits.
- USB HID host (TinyUSB): out of scope, list as a future port item (section 7).
- Prior art to copy most: LVGL partial buffer + async flush callback; 32blit blend function pointers and fixed-step update loop; PicoSystem per-frame draw-state reset; SDL2 streaming texture for the host backend.
- Prior art to avoid: blocking pushes (Arduboy2, PicoSystem single buffer), display-size buffers (LVGL FULL/DIRECT), 4 bit/channel colour (PicoSystem).

## Source quality

LVGL, LovyanGFX, TFT_eSPI, 32blit, PicoSystem, Arduboy2, SDL2 and TinyUSB were read from official GitHub repos and wiki on 2026-11-15. The ILI9341 datasheet V1.11 was read from the adafruit mirror; key tables were read from page images; derived frequencies are 1/period arithmetic. No hardware was measured. All fps are bus ceilings, not promises (K12).

Tags: [verified] source named; [calculated] formula shown; [assumed] judgement; UNKNOWN.

## 1 ILI9341 limits that shape the design

| Item | Value | Source | Tag |
|---|---|---|---|
| 4-line SPI write cycle | twc min 100 ns (SCL high/low each min 40 ns) => max write clock 10 MHz | datasheet 18.3.4 p.243; limit = 1/100 ns | [calculated] |
| SPI read cycle | min 150 ns => ~6.67 MHz | datasheet 18.3.4 | [calculated] |
| Overclocking | Datasheet says nothing; the word does not occur | whole-document grep | [verified] |
| Clocks used in practice | IDF example: 10 MHz default, 26 MHz with LCD_OVERCLOCK; user's breadboard code: 40 MHz (4x the datasheet limit) | IDF example v6.1.1 (S1.2); repo (S1.1) | [verified] |
| 40 MHz SPI | Out of spec; often works in practice, not guaranteed | judgement | [assumed] |
| 8080 parallel write cycle | twc min 66 ns => ~15.15 MHz WRX | datasheet 18.3.1/18.3.2 pp.238-240 | [calculated] |
| Frame-memory read cycle | 450 ns (~2.2 MHz); reading back from the panel to blend is impractical | datasheet 18.3.1; 1/450 ns | [calculated] |
| Pixel formats | 16 bpp (RGB565, 65K): 2 transfers on 8-bit bus, 1 on 16-bit bus; 18 bpp: 3 / 2 transfers; over SPI 16 bpp = 2 bytes, 18 bpp = 3 bytes | datasheet 7.6.2-7.6.5 | [verified] |
| COLMOD 3Ah | 55h = 16/16 bpp; 66h = 18/18 (default) | datasheet 8.2.33 p.134 | [verified] |
| Window write | CASET 2Ah, PASET 2Bh, RAMWR 2Ch (sets pointers to window start, auto-increment, no length limit); Write_Memory_Continue 3Ch; "window area can be updated selectively" | datasheet 8.2.20-22, 8.2.34, Sec.1 | [verified] |
| MADCTL 36h | MY MX MV ML BGR MH (rotation, mirroring, BGR) | datasheet 8.2.29 | [verified] |
| TE pin | Output exists; enabled by TEON 35h (mode 0: V-blank only; mode 1: V+H blank); default OFF | datasheet 8.2.27-28, 10.1-10.2 | [verified] |
| TE pin on a given module | Whether the module routes it | not in datasheet | UNKNOWN |
| GRAM | 172,800 bytes (240x320x18 bit); panel refreshes itself from GRAM with its internal oscillator (F6h DM default), so partial writes need no full redraw | datasheet Sec.5 p.24; F6h p.192 | [verified] |
| Refresh rate | FRMCTR1 B1h: default RTNA 1Bh = 70 Hz, range 61-119 Hz | datasheet 8.3.2 p.155 | [verified] |
| RGB interface | DOTCLK min cycle 100 ns (max 10 MHz); RGB data is written into GRAM first; commands must go through SPI. The F429 DISCO panel uses RGB + SPI init | datasheet 7.2.1; 18.3.5 p.244 | [verified] |
| DISCO panel timings | not in the datasheet | - | UNKNOWN |
| Byte order | ESP32-DOOMGAD breadboard used a pre-swapped buffer with writePixels(..., bigEndian=true); little-endian MCUs need an RGB565 byte swap for SPI / 8-bit buses | repo (S1.1) | [assumed] from that code |

## 2 Strategy comparison

Cells are [assumed] unless a source is named in brackets.

| Strategy | RAM | CPU | Tearing | Complexity | Prior art / fit |
|---|---|---|---|---|---|
| Full framebuffer | W x H x bpp | Low per pixel, one render pass | Needs vsync/flip or copy at blanking | Low | LVGL FULL redraws everything, buffer = display size; LVGL DIRECT keeps the whole image, redraws only the changed area, flush_cb often just swaps address [LVGL setup.mdx]. PicoSystem: one 240x240 buffer (84 kB more than 120x120 pixel-double mode), drawing blocks while flip DMA runs [PS picosystem.cpp, README]. 32blit: game draws into one `screen` buffer; LTDC line IRQ at scanline 252 triggers DMA2D copy into the LTDC RGB565 buffer [32b display.cpp]. Fits DISCO (SDRAM). |
| Partial / band | W x lines x bpp | Render code re-run per band (R1, unmeasured) | Bands sent in order; tearing depends on TE/flush timing | Medium | LVGL PARTIAL, "at least 1/10 display size recommended"; example 320x240 buffer 1/10 = 15,360 B at 2 B/px [LVGL setup.mdx, porting.mdx]. LVGL-LovyanGFX bridge: 40-row buffer, pushImageDMA, RGB565_SWAPPED [lovyan.mdx]. Fits small-RAM MCUs. |
| Scanline | Smallest (band with N = 1) | High per-line overhead | Low risk | Medium | 32blit TileMap::draw takes a per-scanline Mat3 callback (mode7 / affine style) [32b tilemap.hpp]. |
| Tile-based | Band-sized | Tile blits, cheap for regular maps | Same as band | Medium | 32blit TileMap with per-tile transform byte, load_tmx [32b tilemap.hpp]. PicoSystem has no tilemap in the headers read; Arduboy2 has no tile helper [grep]. Fits scrolling tile worlds. |
| Dirty rectangles | Full buffer or band + list | Saves work on static scenes only | Window writes avoid full redraw | High (merge rules, alignment) | LVGL invalidates areas via lv_obj_invalidate, refreshed every LV_DEF_REFR_PERIOD; exact merge rule not documented (UNKNOWN); area rounding event for alignment limits [LVGL refreshing.mdx, redraw_area.mdx]. ILI9341 window writes support it [datasheet Sec.1]. A scrolling camera dirties the whole screen; gain is for static scenes / UI. |
| Sprite batching | Sprite sheets + lists | Culled per band | n/a | Medium | 32blit sprite() with sheet, transform flags HORIZONTAL/VERTICAL/XYSWAP, blit/stretch_blit, blend function pointers (RGBA_RGB565, P_P, M_M) [32b surface.hpp, blend.hpp]. PicoSystem blend_func_t takes a span (src, offset, step, dst, count), HFLIP/VFLIP only, no rotation [PS picosystem.hpp]. Arduboy2 Sprites draw modes drawOverwrite/drawErase/drawSelfMasked/drawPlusMask [AB Sprites.h]. |
| DMA-assisted flush | One extra band (double buffer) | CPU renders while DMA sends | Depends on flush timing | Medium to high | See list below. |
| Direct-to-display | None | Per-draw window setup | None by design, but visible partial draws | Low | No read-back (frame memory read 450 ns), so no blending against the screen [datasheet 18.3.1]. DISCO Core demo draws straight into the LTDC framebuffer with BSP_LCD calls [S1.1]. PicoSystem/Arduboy2 push a whole buffer; Arduboy display() blocks the CPU [AB Arduboy2Core.cpp]. Fallback for tiny RAM only. |

DMA-assisted flush facts:
- LVGL: two buffers render while the other is sent by DMA; three buffers remove CPU idle [setup.mdx].
- LVGL STM32 DMA2D draw unit only does fills and images, no gradient / transform / radius; the rest goes to software [stm32/dma2d_gpu.mdx].
- ESP PPA only on ESP32-P4, needs 64-byte alignment, no gain in partial mode [espressif ppa.mdx].
- 32blit uses DMA2D for pixel-format conversion / copy and a 4-step chain for 2x lores upscale into the 320x240 LTDC buffer [32b display.cpp].
- TFT_eSPI pushImageDMA with two 128x128 16-bit sprites toggled; ESP32 27 MHz SPI: 90 fps non-DMA vs 101 fps DMA; DMA sprite max ~181x181 (64 kB) [SpriteRotatingCube.ino header].
- LovyanGFX pushImageDMA requires waitDMA() before touching the buffer [LGFX_Sprite.hpp].
- ESP32-S3 DMA rules (S1.2 doc): SPI DMA buffers in internal RAM, 32-bit aligned, max_transfer_sz default 4092 B. A 320x16 RGB565 band (10,240 B) needs max_transfer_sz set or the transfer split.
- The repo STM32F429I-DISCO has DMA2D enabled in hal_conf but never called [S1.1].

## 3 Memory tables (calculated)

Formulas: pixels = W x H; bytes = pixels x bytes_per_pixel (8-bit = 1, RGB565 = 2, RGB888 = 3). All values in bytes unless noted.

Frame:

| Size | Pixels | 8-bit | RGB565 | RGB888 |
|---|---|---|---|---|
| 160x128 | 20,480 | 20,480 | 40,960 | 61,440 |
| 240x240 | 57,600 | 57,600 | 115,200 | 172,800 |
| 320x240 | 76,800 | 76,800 | 153,600 | 230,400 |
| 480x320 | 153,600 | 153,600 | 307,200 | 460,800 |

Band: bytes = W x lines x bytes_per_pixel; double buffering = x2.

| Format | Width | 8 lines | 16 lines | 32 lines |
|---|---|---|---|---|
| RGB565 | 160 | 2,560 | 5,120 | 10,240 |
| RGB565 | 240 | 3,840 | 7,680 | 15,360 |
| RGB565 | 320 | 5,120 | 10,240 | 20,480 |
| RGB565 | 480 | 7,680 | 15,360 | 30,720 |
| 8-bit | 160 | 1,280 | 2,560 | 5,120 |
| 8-bit | 240 | 1,920 | 3,840 | 7,680 |
| 8-bit | 320 | 2,560 | 5,120 | 10,240 |
| 8-bit | 480 | 3,840 | 7,680 | 15,360 |
| RGB888 | 160 | 3,840 | 7,680 | 15,360 |
| RGB888 | 240 | 5,760 | 11,520 | 23,040 |
| RGB888 | 320 | 7,680 | 15,360 | 30,720 |
| RGB888 | 480 | 11,520 | 23,040 | 46,080 |

Bands per frame = ceil(H / lines):

| Height | 8 lines | 16 lines | 32 lines |
|---|---|---|---|
| 128 | 16 | 8 | 4 |
| 240 | 30 | 15 | 8 |
| 320 | 40 | 20 | 10 |

Each band re-runs the render callback (R1), so render overhead scales with this count.

Reference sizes on the user's boards [S1.2]: ESP32-S3 internal SRAM 512 KB, 8 MB PSRAM; F429 256 KB SRAM (64 KB CCM) + 8 MB SDRAM. A full 320x240 RGB565 frame (153,600 B) fits in ESP32-S3 internal RAM only by taking about 30% of it [calculated: 153,600 / 524,288, with 512 KB = 524,288 B].

## 4 Bus throughput tables (calculated)

Formula: fps ceiling = bus_bits_per_s / (pixels x bits_per_pixel), RGB565 = 16 bit; no command / CS overhead. A SPI display link stays 16-bit even if the game buffer is 8-bit indexed, so indexed saves RAM, not bus.

SPI fps ceilings:

| SPI clock | 160x128 | 240x240 | 320x240 | 480x320 |
|---|---|---|---|---|
| 10 MHz (ILI9341 datasheet limit) | 30.5 | 10.9 | 8.1 | 4.1 |
| 20 MHz | 61.0 | 21.7 | 16.3 | 8.1 |
| 40 MHz | 122.1 | 43.4 | 32.6 | 16.3 |
| 80 MHz | 244.1 | 86.8 | 65.1 | 32.6 |

Panel controllers other than ILI9341 have their own limits (UNKNOWN; 480x320 panels are not ILI9341).

8080 parallel at the ILI9341 limit 15.15 MHz:

| Bus | 160x128 | 240x240 | 320x240 | 480x320 |
|---|---|---|---|---|
| 8-bit (2 WR cycles per pixel): 15.15e6 / (pixels x 2) | 369.9 | 131.5 | 98.6 | 49.3 |
| 16-bit (1 WR per pixel) | 739.8 | 263.0 | 197.3 | 98.6 |

The ESP32-S3 LCD_CAM limit of 40 MHz [S3 DS 4.2.1.4] is not the binding limit for this panel.

LTDC scan-out read bandwidth at 60 Hz, RGB565, blanking ignored = W x H x 2 x 60: 160x128 2.5 MB/s; 240x240 6.9 MB/s; 320x240 9.2 MB/s; 480x320 18.4 MB/s; 8-bit L8 halves it. DISCO SDRAM peak 160 MB/s [S1.2].

Windowed output idea: a 160x120 logical screen drawn 1:1 into a 160x120 window costs 160 x 120 x 16 = 307,200 bits per frame, a quarter of the 320x240 full frame (1,228,800). That is 4x the fps ceiling (SPI 40 MHz: 130.2 vs 32.6).

2x pixel doubling at flush (K15) saves render CPU but NOT SPI bus time, because the panel still receives 320x240 pixels [assumed, from the panel having no scaler in the datasheet read]. On the DISCO LTDC it is cheap (32blit does it with a DMA2D chain [32b display.cpp]).

## 5 K2 / K3 / K15 verdict

- K2 (band rendering, one path): CONFIRMED. LVGL PARTIAL, LVGL-LovyanGFX 40-row, TFT_eSPI sprites and 32blit all use a buffer smaller than the frame or copy-at-flush; band height is the knob (N = full height gives a classic framebuffer). Amendments proposed for S2:
  - (a) allow band-skip when nothing in a band changed (cheap dirty tracking at band granularity; helps static screens / UI);
  - (b) support windowed flush (letterboxed or 1:1 small logical screen), because SPI bus time, not render CPU, is the ceiling on ILI9341 SPI;
  - (c) async flush with a double band buffer where DMA exists;
  - (d) on DISCO, band to SRAM then DMA2D copy to the SDRAM framebuffer is the intended path [assumed, S3 to measure].
  R1 (per-band re-run cost) stays open until S3.1a.
- K3 (band format RGB565, indexed assets): CONFIRMED. ILI9341 takes RGB565 natively (COLMOD 55h); LTDC takes RGB565; an indexed 8-bit buffer is deferred to G3 (32blit shows L8/CLUT on LTDC is possible). Add: RGB565 byte swap on flush for SPI / 8-bit buses (profile flag byte_swap_on_flush).
- K12 (honest numbers): stands, with a new rule: every bus number states the datasheet limit and the clock actually used. In-spec ILI9341 SPI gives an 8.1 fps ceiling at 320x240; 40 MHz is out of spec.
- K15 (integer scale): stands, but it does not reduce SPI bus time; windowed output is the alternative for SPI panels.
- Other: no change to K1, K4-K11, K13, K14.

## 6 Prior art: what to borrow, what to avoid

LVGL
- Borrow: partial buffer + async DMA flush with flush-ready callback; second buffer for parallel render + DMA; flush-wait callback so the CPU sleeps; area rounding; draw units with software fallback.
- Avoid: DIRECT/FULL need display-size buffers; transform layers cannot be sliced; DMA2D unit only fills/images; PPA is experimental and P4 only; spin-wait unless OS + IRQ; typical UI RAM ~100 kB. [LVGL setup.mdx, stm32/dma2d_gpu.mdx, espressif ppa.mdx]

LovyanGFX
- Borrow: palette sprites 1/2/4/8 bit with palette-index transparent key; setBuffer to wrap caller-owned memory; bus sharing with SD.
- Avoid: DMA push may keep reading the buffer after return (waitDMA); setPsram silently falls back; transparency is a single key (alpha UNKNOWN); no ping-pong example. [LGFX_Sprite.hpp, lovyan.mdx]

TFT_eSPI
- Borrow: ping-pong sprites + pushImageDMA with optional copy buffer; 1-bit sprite as mask.
- Avoid: DMA on SPI only and 16-bit sprites only; max ~181x181 per DMA; PSRAM sprites skipped when DMA enabled; full-screen PSRAM sprite ~45 ms; no 24-bit / 2-bit. [SpriteRotatingCube.ino header]

32blit
- Borrow: fixed 10 ms update with catch-up loop and render gated by a needs_render flag; Surface with pluggable blend function pointers; P palette screen mode via CLUT; TileMap per-scanline callback; sprite transform flags. [32b display.cpp, tilemap.hpp, surface.hpp, blend.hpp]
- Avoid: only 160x120 and 320x240 sizes; D-cache clean/invalidate needed before each DMA flip; palette change delay.

PicoSystem
- Borrow: per-frame draw-state reset (pen, alpha, clip, blend, target, camera); span-based blend function; target(buffer) offscreen; pixel-double option halves RAM. [PS picosystem.hpp, README]
- Avoid: 4 bit/channel colour; single buffer blocks draw during flip; no rotation.

Arduboy2
- Borrow: nextFrame() / setFrameRate() / everyXFrames() frame gate; display(CLEAR_BUFFER); sprite draw modes. [AB Sprites.h, Arduboy2Core.cpp]
- Avoid: blocking push; integer-ms frame period; no tile helper.

SDL2 (the host backend; SDL3 not covered)
- Borrow: streaming texture + LockTexture for a CPU framebuffer; SDL_RenderSetLogicalSize + SDL_RenderSetIntegerScale; nearest scaling is the default; clear then present each frame.
- Avoid: SDL_UpdateTexture is slow per frame; locked texture is write-only; backbuffer invalid after present; INDEX8 texture support UNKNOWN.

## 7 TinyUSB HID host (scope only)

Sources: TinyUSB README, hid.rst, host.rst, FAQ, MIT license.
- Host classes: HID (keyboard, mouse, generic), MSC, CDC, hub, MIDI, audio.
- Ports: STM32F4 host yes via dwc2 (FS vs HS core split UNKNOWN); ESP32-S3 host yes, no high-speed, FAQ warns of S3 limits (detail UNKNOWN); RP2040 native + PIO-USB host.
- Config: CFG_TUH_ENABLED=1; CFG_TUH_HUB default 0; CFG_TUH_HID counts interfaces; report / enumeration buffers 64 / 256 B default.
- Footprint: generic 8-20 KB flash, 1-4 KB RAM; host-specific figure UNKNOWN.
- No RTOS needed (OPT_OS_NONE); call tuh_task() in a loop.
- Callbacks: tuh_hid_mount_cb, tuh_hid_report_received_cb; tuh_hid_receive_report() must be called after mount and after each report.
- Generic gamepad: the stock driver only gives a descriptor summary and the app must parse it; vendor-class (0xFF) pads are not claimed; the hid_controller example decodes only DualShock 4 style pads; XInput UNKNOWN.
- MSC host works with FatFs in the msc_file_explorer example; HID + MSC together is shown in cdc_msc_hid.
- Scope verdict [assumed]: USB HID host stays out of scope (PLAN "Out of scope"), listed as a future port item. The F429 reference repo already uses Cube USB host MSC, not TinyUSB.

## 8 Unresolved

- Exact LVGL dirty-rect merge rule and LV_DEF_REFR_PERIOD default.
- LovyanGFX ping-pong pattern and PSRAM-sprite DMA.
- 32blit panel refresh rate, sheet cell size, mode7.
- PicoSystem tilemap.
- SDL INDEX8 texture support.
- DISCO LTDC panel timings.
- TE pin on the user's SPI module.
- Non-ILI9341 panel limits.
- R1 render-per-band cost (measure in S3.1a).
- R2 tearing handling (S3.2).
- TinyUSB host footprint and F4 FS/HS port detail.
