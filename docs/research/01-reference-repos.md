# 01 Reference repositories (session S1.1, 2026-11-15)

## Summary for later sessions

- [verified] ESP32-DOOMGAD is at design stage. Breadboard firmware is compile-tested only, never run on hardware, and the repo has no engine or Doom code. Source: gh api read of il721/ESP32-DOOMGAD, 2026-11-15.
- [verified] ESP32-DOOMGAD target module is ESP32-S3-WROOM-1-N16R8 (16 MB flash, 8 MB octal PSRAM). Sources: ESP-gadjet-hardware.md; breadboard/src/main.cpp (checkChip gate).
- [verified] ESP32-DOOMGAD display interface is in conflict. The hardware doc targets a 3.2in ILI9341 on 8-bit 8080 parallel. The breadboard and wiring.md use an ILI9341 over SPI (wiring.md says 2.8in, 240x320). Sources: ESP-gadjet-hardware.md; wiring.md; breadboard/src/main.cpp.
- [absent] ESP32-DOOMGAD has no parallel LCD driver, no PPA or 2D blit, no Doom heap or WAD buffer layout, no game sound, no WAD loader, and no stored or measured Doom or SPI fps.
- [verified] ESP32-DOOMGAD input is 8 direct-GPIO buttons, active-low with pull-ups, polled every 10 ms, no ISR, no debounce. Source: breadboard/src/main.cpp.
- [verified] ESP32-DOOMGAD audio is legacy I2S (MAX98357A), 16 kHz 16-bit stereo. The breadboard only plays a 50 ms beep per button press. Source: breadboard/src/main.cpp.
- [verified] ESP32-DOOMGAD architecture is one setup()/loop() file with no modules and no RTOS tasks. Source: breadboard/src/main.cpp.
- [absent] ESP32-DOOMGAD spiBench prints "SPI fps" but stores no result. wiring.md gives only the criterion "roughly 20+ fps feasible", not a measurement. Source: breadboard/src/main.cpp; wiring.md.
- [verified] STM32F429I-DISCO builds with CMake 3.20+ and arm-none-eabi-gcc, with two targets: the Core demo and the DOOM port (STM32F429I_DOOM.elf). SYSCLK is 168 MHz. Sources: cmake/arm-none-eabi.cmake; DOOM/port/doom_main.c.
- [verified] STM32F429I-DISCO DOOM display is LTDC layer 1 RGB565, 240x320, two 150 KB buffers, flipped on vsync via CFBAR and SRCR. The 320x200 8-bit frame is rotated on the CPU. Sources: DOOM/port/doom_main.c:112-131; doom_rotate.h:9-27.
- [verified] STM32F429I-DISCO storage is a USB OTG HS host (Cube USB host MSC + FatFs) with the WAD read on demand from a USB stick. There is no SD slot. Source: DOOM port usb_wad, UsbWad_Read.
- [verified] STM32F429I-DISCO input is touch (STMPE811, polled), six GPIO buttons, and an analog stick on ADC1 (polled, register-level). Gyro is compiled out in the DOOM port. Sources: DOOM port sources; README.
- [verified] STM32F429I-DISCO audio is none (FEATURE_SOUND off).
- [absent] STM32F429I-DISCO DMA2D and DMA are enabled in hal_conf but no DMA or DMA2D call exists in Core/ or DOOM/port/. Sources: hal_conf; Core/; DOOM/port/.
- [verified] STM32F429I-DISCO SDRAM zone allocator runs to 0xD0800000, past the declared SDRAM LENGTH end of 0xD03C0000. No link-time check exists. Sources: STM32F429ZITX_DOOM.ld:54-57; _FLASH.ld:48-50.
- [absent] STM32F429I-DISCO has no measured frame rate, USB read speed, SDRAM refresh figure, or flip result. The README lists these as never run on the board.
- [verified] Neither reference repo has measured performance numbers, so K12 stands. Source: S1.1 decision log.
- [assumed] The principles in the "Principles to reuse" section are proposals, not facts.

## ESP32-DOOMGAD

| Point | Fact | Source | Tag |
|---|---|---|---|
| Hardware capabilities/limits | Target module ESP32-S3-WROOM-1-N16R8 (16 MB flash, 8 MB octal PSRAM). Breadboard checkChip requires PSRAM >= 7 MB and flash >= 16 MB, else prints NOT N16R8. platformio: espressif32@6.12.0, board esp32-s3-devkitc-1, arduino framework, default_16MB.csv partitions, qio_opi, monitor 115200. Libraries: Adafruit ILI9341 1.6.3, Adafruit GFX 1.12.5, Adafruit BusIO 1.17.4. Audio: legacy I2S, 16 kHz 16-bit stereo, DMA 4x256 frames, BCLK 41, LRCK 42, DIN 40 (MAX98357A). Storage: breadboard SD.h on shared FSPI, CS GPIO6, 20 MHz, 1-bit SPI; target is SDMMC 4-bit, 32 GB FAT32 (doc). | ESP-gadjet-hardware.md; breadboard/src/main.cpp; platformio.ini | [verified] (module, libraries, checkChip); SDMMC target = design intent only |
| Display interface + rendering | Breadboard: ILI9341 over SPI (FSPI, IOMUX pins 10-13), tft.begin(40000000), rotation 1; wiring.md says 2.8in SPI TFT 240x320. Hardware doc target: 3.2in ILI9341 320x240 on 8-bit 8080 parallel, D0-D7 on GPIO4-11, WR 12, DC 13, 40-pin header (pinout is a placeholder). Doc and wiring.md conflict. Pixel path: writePixels(..., bigEndian=true) on a pre-swapped buffer. | ESP-gadjet-hardware.md; wiring.md; breadboard/src/main.cpp | [verified] (both descriptions, conflict); parallel driver code [absent]; PPA / 2D blit [absent] |
| Memory management | Bench framebuffer 320x200 RGB565 = 128000 B via ps_malloc (PSRAM), freed after bench. Static 16384 B SD read chunk. Static 3200 B tone buffer. No Doom heap or WAD buffer layout exists. | breadboard/src/main.cpp | [verified] (buffers); Doom heap / WAD layout [absent] |
| Input | 8 buttons on direct GPIO (14, 21, 15, 16, 17, 18, 8, 9), INPUT_PULLUP active-low, polled every 10 ms, no ISR, no debounce. Stick: analogRead GPIO1/2, 12-bit, 40-count deadband; stick press on GPIO9. GPIO9 appears in both the button list and the stick-press note; not resolved. Hardware doc plans a 74HC165 shift register on GPIO 14, 21, 48 for buttons; breadboard does not use it. Key mapping exists only in the doc. | breadboard/src/main.cpp; ESP-gadjet-hardware.md | [verified] (breadboard); 74HC165 path [assumed] (doc only); key mapping in doc only |
| CPU/GPU | No CPU frequency setting and no GPU use were captured in the facts read. Rendering and palette work are done in the ESP32-S3 software path (pre-swapped SPI buffer). | breadboard/src/main.cpp; platformio.ini | [absent] (CPU clock, GPU) |
| DMA/acceleration | I2S DMA is set up through the driver only. SPI pixel path has no explicit SPI DMA setup. Parallel LCD peripheral and SDMMC driver are not used. | breadboard/src/main.cpp; ESP-gadjet-hardware.md | [verified] (I2S via driver; SPI without explicit DMA); [absent] (parallel LCD, SDMMC use) |
| Code architecture / reusable components | Single setup()/loop() file, no modules, no RTOS tasks. Candidate helpers by name: Btn pin table, checkChip, sdTest, spiBench, drawBtn, drawStick. Doc lists prior-art Doom ports as links only: esp-box-emu, DefconBadge2026, rp2040-doom. Only a 50 ms 440 Hz beep per button press (blocking i2s_write, 100 ms timeout); no game sound or mixing. | breadboard/src/main.cpp; ESP-gadjet-hardware.md | [verified]; game sound / mixing [absent] |
| Bottlenecks / improvements | spiBench prints "SPI fps" over BENCH_FRAMES=60 frames of 320x200 but stores no result. wiring.md gives only the criterion "roughly 20+ fps feasible". Doom fps on ESP32-S3 is stated by the doc as not verified. Code costs: serial printf per input event at 115200; delay(10) poll; blocking beep. | breadboard/src/main.cpp; wiring.md; ESP-gadjet-hardware.md | [absent] (measured fps); [verified] (code behaviour) |

## STM32F429I-DISCO

| Point | Fact | Source | Tag |
|---|---|---|---|
| Hardware capabilities/limits | CMake 3.20+, toolchain cmake/arm-none-eabi.cmake (arm-none-eabi-gcc, nano.specs, nosys). Two targets: STM32F429I_DISCO.elf (Core demo, -Og -g3, root linker script) and STM32F429I_DOOM.elf (-O2, DOOM linker script). DISCO_REVD option default OFF. flash_doom target via openocd stlink hla_swd; load_doom.bat/.ps1 wrap build and flash. Clock: PLLM 8, PLLN 336, PLLP 2, PLLQ 7 gives SYSCLK 168 MHz, USB 48 MHz, overdrive on, flash latency 5. USB OTG HS with embedded PHY, external VBUS. hhcd speed is set to HCD_SPEED_HIGH, but a comment and the README say full speed 12 Mbit/s (inconsistent). STM32CubeF4 submodule is required (not read). | CMake files; cmake/arm-none-eabi.cmake; DOOM/port/doom_main.c; README | [verified] (build, clock, targets); CubeF4 contents [absent]; speed inconsistency unresolved |
| Display interface + rendering | LTDC layer 1 RGB565, 240x320 window; two 150 KB buffers; flip by writing LTDC_Layer1->CFBAR then SRCR=VBR (vsync); spins on SRCR before draw. Doom_RotateFrame: 320x200 8-bit to 240x320 RGB565, 90-degree rotate, 1.2x vertical stretch, CPU loop with per-pixel palette lookup; 256-entry pal565 rebuilt when the palette changes. Core demo: LTDC layer 1 ARGB8888 via BSP default, BSP_LCD drawing calls, no DMA2D. DOOM port has never run on hardware (README). | DOOM/port/doom_main.c:112-131; doom_rotate.h:9-27; doom_glue.c:75-85; Core/Src/main.c; Core/Src/game.c; README | [verified] (code); on-board run [absent] |
| Memory management | Linker regions: CCMRAM 64K @0x10000000, RAM 192K @0x20000000, ROM 2048K @0x08000000, SDRAM region @0xD0060000 LEN 0x360000. SDRAM use: FB0 @0xD0000000, FB1 @0xD0030000, engine .bss @0xD0060000 (NOLOAD, cleared by main), zone allocator up to hardcoded ZONE_END 0xD0800000. Observation: declared SDRAM LENGTH ends at 0xD03C0000 but the zone runs to 0xD0800000; no link-time check. Stack and heap: DOOM _Min_Stack_Size 0x4000, _Min_Heap_Size 0x2000; Core stack 0x400, heap 0x200. 8-bit game frame in SRAM is handed to the port with no copy; colormaps are copied to SRAM. | STM32F429ZITX_DOOM.ld:54-57; _FLASH.ld:48-50 (stack/heap symbols; exact lines not recorded); README (frame and colormap note) | [verified] (regions, SDRAM overrun observation); frame/colormap handling [assumed] (README only, not code-checked) |
| Input | Touch STMPE811 via BSP_TS_GetState, polled. Gyro L3GD20 compiled out (GYRO_CONTROL 0) in the DOOM port. Six GPIO buttons, pull-ups, active-low: PA0 USER, PC11 fire, PC12 use, PD2 prev, PD4 next, PD5 esc, polled. Stick ADC: ADC1 register-level (HAL ADC not built), polled, 12-bit, 480-cycle sample, PCLK2/4; VRx PA5 (ch5), VRy PC3 (ch13), SW PB7 pull-up; 64-sample centre at boot. README says VRx = forward, VRy = turn; code swaps the channels (noted in source). Each input poll = 2 blocking ADC conversions (~25 us each). | DOOM port sources; README | [verified] (pins, polling, channel swap); README-only items tagged in the README |
| CPU/GPU | CPU only for the DOOM port: frame rotate and text are software. 168 MHz SYSCLK (see Hardware row). LTDC scans out the frame; no GPU use found. | DOOM/port/doom_rotate.h; doom_main.c | [verified] (CPU path); [absent] (GPU use) |
| DMA/acceleration | dma_enable = 0 for USB. HAL DMA and DMA2D modules are enabled in hal_conf, but no DMA or DMA2D call exists in Core/ or DOOM/port/. Rotate and text are CPU. | hal_conf; Core/; DOOM/port/ | [verified] (modules enabled); [absent] (any DMA or DMA2D use) |
| Code architecture / reusable components | Modular DOOM port: doom_main (board), doom_glue (DG_* hooks, key queue, zone), usb_wad (USB + FatFs), w_file_usb, syscalls_fs. Core demo is an LCD/touch/gyro ball-tilt game at about 30 Hz with no DOOM dependency. DOOM tree is GPLv2. | DOOM/port/; Core/; README | [verified] (layout); ~30 Hz [verified] per facts read |
| Bottlenecks / improvements | No frame cap in the port (DG_SleepMs -> HAL_Delay; pacing is in the engine, not read). Rotate = 76,800 halfword SDRAM writes and 76,800 palette lookups per frame, strided 8-bit reads, no DMA2D. README lists frame rate, USB read speed, SDRAM 84 MHz refresh, and flip as never run on the board. | doom_glue.c; doom_rotate.h; README | [verified] (code cost); [absent] (any measurement) |

## Principles to reuse (not copy)

Proposed principles only. Each is [assumed] until tested on hardware.

- [assumed] LTDC double buffer with vsync flip on CFBAR/SRCR (STM32F429I-DISCO pattern).
- [assumed] DMA2D is unused in the reference, so a clear, blit or format-convert accelerator is an open opportunity.
- [assumed] Palette-indexed assets converted at blit time, not stored as RGB565.
- [assumed] Keep port glue in thin files (board, glue, storage split).
- [assumed] Register-level polled ADC is cheap enough for a stick.
- [assumed] Keep memory regions explicit in the linker script and check them against the declared lengths.
- [assumed] Measure SPI and frame rate before fixing the ESP32 display interface, since no stored numbers exist.
- [assumed] Keep blocking calls (beep, serial prints) out of the frame path.

## Absent / not verified

- ESP32-DOOMGAD: parallel 8080 driver; PPA or 2D blit; Doom heap and WAD buffer layout; game sound; WAD loader; SDMMC driver; stored SPI fps; measured Doom fps; key-mapping implementation; final display interface.
- ESP32-DOOMGAD: CPU clock setting and any GPU use.
- STM32F429I-DISCO: DMA2D use; on-board frame rate, USB read speed, SDRAM refresh figure, flip result; free audio pins (repo does not list them).
- STM32F429I-DISCO: STM32CubeF4 submodule contents; DOOM/engine/** not read; hhcd speed inconsistency unresolved.
- Not read in either repo: ESP32 kicad/ directory, git history, issues.

## Resolved from PLAN.md "Not yet verified" list

- DMA2D use: absent. No DMA2D call exists in Core/ or DOOM/port/.
- Stick ADC: ADC1 register-level, polled (STM32F429I-DISCO).
- Linker memory map: see the Memory management row above; SDRAM zone overruns the declared length (no link-time check).
- SPI bench numbers: none stored in the ESP32-DOOMGAD repo.
- Free audio pins on DISCO: unknown in the repo; deferred to S1.2 (R8).

## Method note

Read via gh api by read-only subagents on 2026-11-15. Not read: submodule STM32CubeF4; DOOM/engine/**; ESP32 kicad/ directory; git history; issues. DOOM/ code is GPLv2; nothing was copied (K13).
