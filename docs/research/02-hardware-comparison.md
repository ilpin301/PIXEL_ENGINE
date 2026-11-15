# 02 Hardware comparison and capability profile (session S1.2, 2026-11-15)

Tag legend: a bracket naming a source, e.g. [DS 4.1.1], means [verified] from that source. [calculated] = formula shown. [assumed] = proposal or inference. UNKNOWN = not found in the sources read.

## Summary for later sessions
- Board ownership: the user owns only STM32F429I-DISCO and ESP32-S3-WROOM-1-N16R8 (per Q2). All other chips (ESP32, STM32F4 range, H743/H750, RP2040, RP2350) are Tier B, datasheet-only.
- SPI ceilings (bus-limited, 320x240 RGB565 = 1,228,800 bits/frame [calculated]): 20 MHz = 16.3 fps; 40 MHz = 32.6; 80 MHz = 65.1; F429 SPI1 45 Mbit/s = 36.6; F429 SPI2/3 22.5 Mbit/s = 18.3; RP2040 62.5 Mb/s = 50.9; RP2350 70.5 Mb/s = 57.4 [calculated, section 4].
- Parallel ceiling: ESP32-S3 LCD_CAM "operate at 40 MHz or lower" [DS 4.2.1.4]: 8-bit = 260.4 fps, 16-bit = 520.8 fps [calculated]; the real limit is the panel write cycle (ILI9341 datasheet not read, UNKNOWN, check in S1.3).
- All figures are ceilings, not promises (K12). The user's breadboard SPI bench result does not exist (S1.1). The breadboard code uses 40 MHz [repo, verified in S1.1].
- ESP32-S3 DMA: GDMA 5 TX + 5 RX channels shared by SPI2/3, UHCI0, I2S0/1, LCD/CAM, AES, SHA, ADC, RMT; can reach internal and external RAM [DS 4.1.1.4].
- ESP32-S3 PSRAM rules: 8 MB octal PSRAM, GPIO35/36/37 taken [WROOM-1 DS Tbl 1]. GDMA to PSRAM is burst-only, block 16/32/64 B; RX address and size must be block-aligned, TX needs no alignment; descriptors cannot be in PSRAM [TRM 3.4.9; IDF external-ram].
- ESP32-S3 more PSRAM rules: DMA bandwidth "very limited" when the CPU also uses PSRAM; GPSPI bandwidth must stay below PSRAM bandwidth or data may be lost [IDF external-ram; spi_master]. PSRAM unreachable while flash cache is disabled; accesses >32 KB fall back to PSRAM speed [IDF external-ram].
- ESP32-S3 SPI DMA: max_transfer_sz defaults to 4092 bytes; buffers MALLOC_CAP_DMA internal RAM and 32-bit aligned (else driver copies) [IDF spi_master]. Use MALLOC_CAP_SPIRAM|MALLOC_CAP_DMA for PSRAM DMA buffers [IDF mem_alloc].
- F429 DMA2D: fill, copy, copy with pixel-format conversion, blend with conversion; own CLUT [DS 3.11]. LTDC: up to XGA, 24-bit RGB, 2 layers, CLUT 256x24, alpha blend [DS 3.10].
- F429 DISCO display: the 2.4in 240x320 ILI9341 is driven by LTDC RGB, SPI only for init, not FMC [UM 6.9]. SDRAM 8 MB, 80 MHz, 16-bit [UM 6.10]. LTDC scan-out at 60 Hz = 9.2 MB/s of the 160 MB/s SDRAM peak [calculated].
- DISCO audio answer (R8): no on-board codec; PE2/4/5/6 form a free SAI1 block A, so a MAX98357A-class I2S amp is feasible; PWM via TIM9 is the fallback; DAC PA5 conflicts with the stick ADC (section 6). Resolved by datasheet [assumed], to be confirmed on the board in S3.
- Source-quality caveat: the STM32 numbers come from mirrors and third-party copies, not st.com (see Source quality). Re-check before relying on them. ESP32/S3 and RP numbers come from official docs.
- Unresolved (details in section 7): ESP32 TRM fapb/2 vs IDF 80 MHz; ILI9341 max SPI clock and write cycle; H743 vs H750 FMC rating; H7 cache-maintenance text; F429 SRAM1/2/3 split; RP2040 PSRAM; SDSPI on both ESP chips; errata unread.

## Source quality
ESP32/S3 and RP: official Espressif/Raspberry Pi docs read in full. STM32: st.com PDF downloads timed out, so ST datasheet DS Rev 8 (2016, F427/429) and UM1670 Rev 5 came from university mirrors, RM0090/RM0433 text from unofficial mirror stm32.dirba.io, H7 datasheets DS12556 Rev7 (2023) and DS12110 Rev7 (2019) from third-party PDF copies. Re-check STM32 numbers against st.com before relying on them. ES0392/H7 and F4 errata not read.

## 1 Espressif: ESP32 and ESP32-S3
Sources: ESP32 DS v5.3, TRM v5.9, ESP-IDF v6.1.1 docs; S3 DS v2.2, WROOM-1 DS v1.8, TRM v1.8.

| Item | ESP32 (classic) | ESP32-S3 (N16R8 module) | Source/tag |
|---|---|---|---|
| CPU | 1 or 2 Xtensa LX6, up to 240 MHz | Dual Xtensa LX7 up to 240 MHz, 128-bit data bus, SIMD/PIE | [DS 4.1.1] / [DS 4.1.1.1] |
| FPU | "support for FPU", precision UNKNOWN | single-precision; double UNKNOWN | [ESP32 DS]; [S3 DS] |
| Internal SRAM | 520 KB + 16 KB RTC | 512 KB (GDMA range 480 KB), 16 KB RTC, ROM 384 KB | [DS Features]; [DS 4.1.2.1; TRM 3.4.8] |
| Caches | UNKNOWN | I 16/32 KB, D 32/64 KB | [S3 DS 4.1.2.3] |
| External RAM | up to 4 MB mapped (QSPI); more needs himem; at 80 MHz occupies HSPI or VSPI | 8 MB octal PSRAM in package, max 80 MHz (120 MHz option with restrictions); GPIO35/36/37 used | [IDF external-ram]; [WROOM-1 DS Tbl 1, pin note] |
| PSRAM caveats | shares flash cache region; chunks >32 KB fall back to PSRAM speed; unreachable while flash cache disabled; rev v1.0 silicon needs -mfix-esp32-psram-cache-issue | >32 KB accesses fall back to PSRAM speed; unreachable while flash cache disabled; PSRAM and flash share a bus | [IDF external-ram]; [IDF external-ram, rgb_lcd] |
| Flash | external QSPI up to 16 MB mappable | 16 MB quad flash, 80 MHz default | [DS 4.1.2]; [WROOM-1 DS] |
| DMA | SPI1-3 share two DMA channels; I2S/UART/SDIO/EMAC have DMA; no GDMA | GDMA 5 TX + 5 RX channels shared by SPI2/3, UHCI0, I2S0/1, LCD/CAM, AES, SHA, ADC, RMT; reach internal and external RAM | [TRM 20.1]; [DS 4.1.1.4] |
| DMA memory | buffers in DMA-capable internal memory, excludes PSRAM | PSRAM burst-only, block 16/32/64 B; RX address+size block-aligned; TX no alignment; descriptors not in PSRAM; internal RAM descriptor covers 1-4095 bytes | [IDF mem_alloc, spi_master]; [TRM 3.4.8, 3.4.9; IDF external-ram] |
| SPI master clock | HSPI/VSPI up to fapb/2 (APB 80 MHz = 40 MHz); IDF says 80 MHz on IO_MUX pins, 40 MHz via GPIO matrix (not reconciled) | SPI2/3 master up to 80 MHz, SPI2 also 8-line; slave up to 60 MHz; at <=40 MHz GPIO matrix = IOMUX | [TRM 20.4; IDF spi_master]; [DS 4.2.1.5; IDF spi_master] |
| SPI no-DMA burst | 64 bytes per burst | UNKNOWN | [TRM 20.1] |
| Parallel LCD | I2S "LCD mode" master transmit; 8/16-bit width UNKNOWN; esp_lcd i80 on ESP32 UNKNOWN; RGB LCD UNKNOWN (not listed) | LCD_CAM 8-16 bit RGB / I8080 / MOTO6800, "operate at 40 MHz or lower"; esp_lcd i80 bus_width 8 or 16 only; esp_lcd RGB 8/16/24 bit, framebuffer in PSRAM via EDMA or bounce buffer in internal RAM | [ESP32 DS]; [DS 4.2.1.4; IDF i80_lcd, rgb_lcd] |
| Timers | 2 groups x 2 64-bit | four 54-bit GP timers, 52-bit system timer | [DS]; [DS 4.1.3] |
| ADC | two 12-bit SAR, 18 channels, DIG 2 Msps / RTC 200 ksps; ADC2 shared with Wi-Fi | two 12-bit SAR, 20 channels, ADC2 not with Wi-Fi | [DS 4.9.1; IDF adc_oneshot]; [DS 4.2.2.1; IDF] |
| Audio | 2 I2S with PDM, 2x8-bit DAC on GPIO25/26, only I2S0 supports on-chip DAC/ADC | 2 I2S 8-32 bit, TDM and PDM, no DAC | [DS 4.8.5, 4.9.2]; [DS 4.2.1.3] |
| USB | none in DS feature list | FS OTG (device+host) plus USB Serial/JTAG | [ESP32 DS]; [DS 4.2.1.7-8] |
| SDMMC | 1 host, 2 cards, up to 80 MHz, 1/4/8-bit; SDSPI UNKNOWN | 2 slots up to 80 MHz; SDSPI UNKNOWN | [DS 4.8.10]; [DS 4.2.1.9] |

ILI9341 SPI clock on ESP (S3 and classic):
| Item | Value | Source/tag |
|---|---|---|
| Espressif ILI9341 maximum | UNKNOWN (no Espressif doc states one) | UNKNOWN |
| esp_lcd pclk_hz | "should not exceed the range recommended in the LCD spec" | [IDF esp_lcd] |
| IDF example spi_master/lcd | 10 MHz default, 26 MHz with CONFIG_LCD_OVERCLOCK | [IDF example source v6.1.1] |
| User's breadboard code | 40 MHz | [repo, verified in S1.1] |
| SPI DMA transaction size | max_transfer_sz defaults to 4092 bytes with DMA; spi_bus_get_max_transaction_len() returns the real limit; example uses 16 lines*320*2+8 bytes | [IDF spi_master] |

## 2 STM32
### 2.1 STM32F429ZIT6 (DISCO MCU)
Sources: DS Rev 8 Jan 2016 (mirror), UM1670 Rev 5 (mirror; board text says STM32F429I-DISC1 with ST-LINK/V2-B, MB1075, replaces DISCO), RM0090 mirror.

| Item | Value | Source/tag |
|---|---|---|
| CPU | Cortex-M4 180 MHz, MPU, DSP, ART | [DS p1] |
| FPU | yes, precision not stated | UNKNOWN |
| SRAM | 256 KB system incl. 64 KB CCM ("112+16+64+64") + 4 KB backup; SRAM1/2/3 name mapping UNKNOWN | [DS Table 2, 3.6] |
| Flash | 2 MB two banks | [DS p1] |
| FMC | 4 chip selects, 8/16/32-bit, SDRAM/SRAM/PSRAM/NOR/NAND, FMC_SDCLK max 90 MHz; Intel 8080 / Motorola 6800 modes | [DS 3.9] |
| DISCO SDRAM | 64 Mbit (1M x16 x4 banks) = 8 MB, 80 MHz, FMC bank 2 | [UM 6.10, Table 7] |
| DMA | DMA1+DMA2, 8 streams each (16) with FIFOs, burst, circular, double-buffer | [DS 3.8] |
| DMA2D | rect fill, rect copy, copy with pixel-format conversion, blend with conversion; 4 bpp indexed to 32 bpp; own CLUT | [DS 3.11] |
| LTDC | up to XGA 1024x768, 24-bit RGB, 2 layers, 64x32-bit FIFO each, CLUT 256x24 per layer, alpha blend, colour key | [DS 3.10] |
| LTDC formats | ARGB8888, RGB888, RGB565, ARGB1555, ARGB4444, L8, AL44, AL88 | [RM0090 16.2 via mirror] |
| DISCO LCD | 2.4in 240x320 ILI9341 via RGB (LTDC) + SPI for init, not FMC | [UM 6.9] |
| SPI | 6; SPI1/4/5/6 up to 45 Mbit/s, SPI2/3 22.5 Mbit/s | [DS 3.25] |
| Timers | 10 GP + 2 advanced + 2 basic, headline "up to 17" | [DS Table 2] |
| ADC | 3x12-bit, 2.4 MSPS each, 7.2 MSPS triple interleaved; channel count for ZI UNKNOWN | [DS 3.38] |
| DAC | 2x12-bit, DAC_OUT1=PA4, DAC_OUT2=PA5 (shared with ADC12_IN4/5) | [DS 3.40, Table 10] |
| Audio | I2S2/I2S3 (muxed with SPI2/3, full duplex), 8-192 kHz; SAI1 two sub-blocks | [DS 3.26-3.29] |
| USB | OTG FS (on-chip PHY); OTG HS (ULPI; on board the HS core uses the internal FS PHY, so FS only) | [DS p1; UM 6.7] |
| SDIO | 1/4/8-bit up to 48 MHz | [DS 3.30] |

### 2.2 STM32F4 range [ST product pages]
| Part | MHz | FPU | RAM | Flash | LTDC | DMA2D | FMC |
|---|---|---|---|---|---|---|---|
| F401 | 84 | yes | 96 KB | 128-512 KB | UNKNOWN (not listed) | UNKNOWN | UNKNOWN |
| F411 | 100 | yes | up to 128 KB | 256-512 KB | UNKNOWN (not listed) | UNKNOWN | UNKNOWN |
| F429 | 180 | yes | 256 KB (+4 KB backup) | 512 KB-2 MB | yes | yes | yes |
| F446 | 180 | yes | 128 KB | 256-512 KB | UNKNOWN (not listed) | UNKNOWN | yes |
| F469 | 180 | yes | 384 KB | 512 KB-2 MB | yes | yes | yes |

### 2.3 STM32H743 / H750
Sources: ST product pages (PP); DS12556 Rev 7 (H750, "DS750"); DS12110 Rev 7 (H743); RM0433 mirror.

| Item | Value | Source/tag |
|---|---|---|
| CPU | Cortex-M7 up to 480 MHz (needs revision V silicon; 400 MHz also listed), 16 KB I + 16 KB D cache | [PP; DS750] |
| FPU | double-precision (also single) | [PP; DS750] |
| Flash | H743 up to 2 MB dual bank; H750 128 KB single bank | [PP; DS750] |
| SRAM | 1 MB total: ITCM 64 KB 0x0000_0000, DTCM 128 KB 0x2000_0000, AXI 512 KB 0x2400_0000, SRAM1 128 KB 0x3000_0000, SRAM2 128 KB 0x3002_0000, SRAM3 32 KB 0x3004_0000, SRAM4 64 KB 0x3800_0000, backup 4 KB | [DS750 3.3.3; RM 2.4] |
| DMA reach | AXI SRAM and SRAM1/2/3 reachable by all masters except BDMA; DTCM/ITCM only by CPU (and MDMA via the CPU AHBS port): DMA1/2, DMA2D, LTDC, SDMMC, Ethernet, USB cannot | [RM 2.1.2, 2.4, Tbl 3] |
| LTDC / DMA2D reach | LTDC reaches only AXI SRAM, flash, QUADSPI, FMC; RM calls AXI SRAM "reserved for graphic objects such as frame buffers"; DMA2D also reaches SRAM1/2/3 | [RM Tbl 3, 2.4] |
| FMC | 8/16/32-bit, SDRAM + SRAM/PSRAM/NOR/NAND, sync clock = kernel clock/2; rating up to 100 MHz (H743 page) vs 133 MHz (H750 page), reason UNKNOWN; SDRAM not on H750 LQFP144; 8080-style drive UNKNOWN (RM ch 22 has no 8080/6800 text) | [PP; DS750 Tbl 1 fn 1] |
| QUADSPI | QUADSPI only (no Octo-SPI), memory-mapped at 0x9000_0000 up to 256 MB, up to 133 MHz | [DS750 3.16; PP] |
| DMA | MDMA (16 ch linked list), DMA1/DMA2, BDMA, DMAMUX | [DS750 3.10] |
| DMA2D | same modes as F4, plus YCbCr and two CLUTs | [DS750 3.11; RM ch 18] |
| LTDC | 24-bit RGB up to XGA, 2 layers, 64x64-bit FIFO, CLUT 256x24, same formats as F4; pixel clock output 150 MHz at 2.7-3.6 V, 20 pF | [DS750 Tbl 100; RM ch 32] |
| SPI | 6 SPI; master 133 MHz on SPI1/2/3, 100 MHz on SPI4/5/6 at 2.7-3.6 V; FIFO 16 B (SPI1/2/3), 8 B (SPI4/5/6) | [DS750 Tbl 104; RM ch 50] |
| ADC / DAC | 3x16-bit up to 3.6 MSPS, up to 36 channels (per-package split UNKNOWN); DAC 2x12-bit | [PP] |
| Audio | I2S 3 + 1 low-power; 4 SAI | [DS750 3.34-3.35] |
| USB | OTG_FS and OTG_HS (HS needs external ULPI PHY for 480 Mbit/s) | [DS750 3.41] |
| SDMMC | 2 interfaces up to 125 MHz | [DS750 3.39] |
| Cache attributes | RM0433 has no DMA/LTDC/DMA2D coherency text (UNKNOWN); defaults: SRAMs write-back write-allocate, QUADSPI write-through, FMC SDRAM Device type, MPU-reconfigurable | [RM Tbl 7] |
| Timers | product page "up to 22" vs DS750 Table 1 (HRTIM + 10 GP + 2 adv + 2 basic + 5 LP); not reconciled | [PP; DS750 Table 1] |

## 3 Raspberry Pi: RP2040 and RP2350
Sources: official datasheets (RP2350 build 80d627281ed5).

| Item | RP2040 | RP2350 | Source/tag |
|---|---|---|---|
| CPU | 2x Cortex-M0+, 133 MHz max (200 MHz stated at 1.15 V core) | 2x Cortex-M33 or 2x Hazard3 RV32IMAC+, 150 MHz max; overclock rating UNKNOWN | [DS 1.2; 2.15.3]; [DS 1; 12.11; 12.3] |
| FPU | no hardware FPU stated (inference [assumed]); ROM has optimised single-precision float library | M33 single-precision; Hazard3 none; double via per-core DCP coprocessor (add/sub/mul/div/sqrt, 2 instances) | [DS 2.8.3.2]; [DS 3.6.2, 3.6.4] |
| SRAM | 264 kB, 6 banks (4x64 kB striped + 2x4 kB) | 520 kB, 10 banks (8x64 kB + 2x4 kB), striped, single-cycle | [DS 2.6.2]; [DS 4.2] |
| Flash / XIP | external QSPI only, up to 16 MB, SPI clock max half sysclk; stateless | none on RP2350A/B (2 MB stacked on RP2354); XIP up to 16 MB per QMI chip select, 16 kB cache; 8 kB OTP | [DS]; [DS 1, 4.4, 4.5] |
| PSRAM | no chip select described, UNKNOWN | via QMI second chip select (CS1 alternate GPIO), 16 MB each, 32 MB total, read/write memory-mapped, IOVDD 3.3 V; QMI SCK divisor 1-256 of clk_sys, absolute max SCK UNKNOWN | UNKNOWN; [DS 4.4, 12.14] |
| DMA | 12 channels | 16 channels, 32/16/8-bit, chaining, pacing timers, DREQ_HSTX | [DS 2.5]; [DS 12.6] |
| PIO | 2 blocks x 4 SM = 8 SM | 3 blocks x 4 SM = 12 SM; uses incl. 8080/6800 parallel, SPI/QSPI, I2S, SDIO, DPI/VGA (DPI sustains 360 Mb/s at 48 MHz sysclk) | [DS 3.1]; [DS 11.1] |
| Video | no HSTX, no TMDS encoder | HSTX output-only GPIO12-19, DDR up to 300 Mb/s per pin, DMA-fed; DVI-D example dvi_out_hstx_encoder (3:3:2 RGB); TMDS encoder in SIO, one per core | [DS 2.3.1]; [DS 12.11, 3.1.9; pico-examples] |
| SPI | 2 controllers, master peak 62.5 Mb/s at 133 MHz | 2 controllers, master peak 70.5 Mb/s at 150 MHz clk_peri | [DS 4.4]; [DS 12.3] |
| Timers / PWM | one 64-bit 1 us timer, 4 alarms; 16 PWM channels | 2x64-bit 1 us system timers; 12 PWM slices (24 ch) | [DS 4.6, 4.5]; [DS 12.8, 12.5] |
| ADC | 12-bit 500 ksps, 4 channels + temp | 12-bit (9.2 ENOB) 500 kS/s, 4 inputs (QFN-60) or 8 (QFN-80) | [DS 4.9]; [DS 12.4] |
| Audio | PWM; I2S via PIO | PWM; I2S via PIO | [DS 4.5, 3.1]; [DS 12.5, 11.1] |
| USB | 1 controller FS device / FS-LS host | 1 USB 2.0 controller FS device / FS-LS host, 1.1 PHY; PIO-USB is third-party, not in official datasheets (UNKNOWN officially) | [DS 4.1]; [DS 12.7] |
| Storage | external QSPI flash | SD via SPI or PIO SDIO | [DS]; [DS 11.1] |
| Other | 2 interpolators per core | 2 interpolators per core; blitter / 2D engine: none found (UNKNOWN) | [DS 2.3.1]; [DS 3.1.10] |

## 4 Display bus bandwidth limits (calculated)
- Frame bits = 320 x 240 x 16 = 1,228,800 bits (153,600 B) [calculated].
- Max fps (bus-limited, one bit per clock, no overhead) = bus_bits_per_s / 1,228,800 [calculated].

| Bus | Rate | fps ceiling | Tag |
|---|---|---|---|
| SPI 20 MHz | 20e6 | 16.3 | [calculated] |
| SPI 40 MHz | 40e6 | 32.6 | [calculated] |
| SPI 80 MHz | 80e6 | 65.1 | [calculated] |
| STM32F429 SPI1 | 45e6 (DS 3.25) | 36.6 | [calculated] |
| STM32F429 SPI2/3 | 22.5e6 (DS 3.25) | 18.3 | [calculated] |
| RP2040 SPI | 62.5e6 (DS 4.4) | 50.9 | [calculated] |
| RP2350 SPI | 70.5e6 (DS 12.3) | 57.4 | [calculated] |
| ESP32 classic GP-SPI | fapb/2 = 40e6 | 32.6 | [calculated], APB 80 MHz [assumed] |

- 480x320 RGB565 = 2,457,600 bits: 40 MHz SPI = 40e6 / 2,457,600 = 16.3 fps [calculated].
- 8-bit parallel (8080) at the LCD_CAM 40 MHz limit [verified, ESP32-S3 DS]: 40e6 x 8 = 320 Mbit/s => 320e6 / 1,228,800 = 260.4 fps ceiling [calculated]; 16-bit => 520.8 fps [calculated]. Real ceiling is the panel write-cycle limit; ILI9341 datasheet not read (UNKNOWN, check in S1.3).
- LTDC scan-out read bandwidth at 60 Hz, 320x240 RGB565, blanking ignored = 320 x 240 x 60 x 2 B = 9.2 MB/s [calculated].
- DISCO SDRAM peak = 80 MHz x 16 bit = 1,280 Mbit/s = 160 MB/s [calculated; width from UM 6.10; refresh and row overhead ignored].
- Real SPI fps is lower (command overhead, CPU/DMA gaps). The user's breadboard SPI bench result does not exist (S1.1). All figures are ceilings, not promises (K12).

## 5 Capability profile field list (K14, proposed [assumed])
Fields: cpu_mhz; has_fpu (none/single/double); ram_budget_bytes (usable internal RAM for engine); band_lines; band_buffer_in_dma_memory (bool); external_ram (none/sdram/psram) + dma_reachable flag; display_bus (spi/8080/ltdc/rgb) + max_bus_hz; has_2d_dma (DMA2D-class fill/copy/convert); dma_alignment_bytes; max_dma_transfer_bytes; cache_coherency_required (bool); byte_swap_on_flush (bool); audio_out (none/pwm/dac/i2s); storage (usb_msc/sdmmc/sdspi/flash); input_adc_bits; tick_source_hz.
Derived switches: lighting_enabled, audio_enabled, parallax_layers, chunk_cache_count, integer_scale_max (K15).

Tier A values below are [assumed] mappings of the datasheet facts in sections 1-2; "host" values depend on the PC build and are not researched here (UNKNOWN). Tier B boards: see sections 1-3.

| Field | host (PC) | F429I-DISCO (Tier A) | ESP32-S3 N16R8 (Tier A) |
|---|---|---|---|
| cpu_mhz | UNKNOWN | 180 [DS p1] | up to 240 [DS 4.1.1] |
| has_fpu | UNKNOWN | yes, precision UNKNOWN | single [S3 DS] |
| ram_budget_bytes | UNKNOWN | internal 256 KB incl. 64 KB CCM (CCM not DMA-reachable: UNKNOWN, RM not read) [DS Table 2] | internal 512 KB [DS 4.1.2.1] |
| band_buffer_in_dma_memory | n/a | yes if in SRAM; DMA reach for F4 CCM UNKNOWN | must be internal DMA-capable RAM [IDF spi_master] |
| external_ram | UNKNOWN | sdram 8 MB, 80 MHz [UM 6.10] | psram 8 MB octal [WROOM-1 DS]; DMA burst/alignment rules [TRM 3.4.9] |
| display_bus | UNKNOWN | ltdc RGB (+SPI init) [UM 6.9]; FMC 8080 mode exists [DS 3.9] | spi up to 80 MHz [DS 4.2.1.5]; i80/RGB via LCD_CAM <=40 MHz [DS 4.2.1.4] |
| has_2d_dma | n/a | yes, DMA2D [DS 3.11] | no (none listed; UNKNOWN) |
| dma_alignment_bytes | n/a | UNKNOWN (RM not read) | 32-bit SPI DMA buffers [IDF spi_master]; 16/32/64 B PSRAM block [TRM 3.4.9] |
| max_dma_transfer_bytes | n/a | UNKNOWN | 4092 default SPI [IDF spi_master]; 4095 per descriptor [TRM 3.4.8] |
| cache_coherency_required | n/a | no data cache on M4 [assumed, not stated in sources read] | yes for PSRAM DMA; handled by MALLOC_CAP_SPIRAM\|MALLOC_CAP_DMA [IDF mem_alloc] |
| byte_swap_on_flush | n/a | depends on LTDC format RGB565 [RM0090 16.2] | depends on SPI/i80 byte order, UNKNOWN |
| audio_out | UNKNOWN | i2s/SAI1 via PE4/5/6, pwm TIM9, dac PA5 conflict (section 6) | i2s (no DAC) [DS 4.2.1.3] |
| storage | UNKNOWN | usb_msc (FS) [DS p1], sdmmc via SDIO [DS 3.30] | usb FS OTG [DS 4.2.1.7], sdmmc [DS 4.2.1.9] |
| input_adc_bits | n/a | 12 [DS 3.38] | 12 [DS 4.2.2.1] |
| tick_source_hz | UNKNOWN | UNKNOWN (SysTick/TIM, not researched) | 1 MHz-class system timer UNKNOWN (52-bit, rate not stated) [DS 4.1.3] |

## 6 F429I-DISCO audio pins (answer to R8)
Source: UM1670 Rev 5 Table 7/6 via mirror, DS Rev 8 Table 10 AF; AF numbers not re-read from st.com. The board in the UM is DISC1 MB1075 with I3G4250D gyro; the user's board may be an older DISCO with L3GD20. Re-check against the board's own UM before wiring.
- No on-board audio codec [UM 1670 Rev 5].
- Free and audio-capable: PE2 SAI1_MCLK_A (P1-15), PE3 SAI1_SD_B (P1-16), PE4 SAI1_FS_A (P1-13), PE5 SAI1_SCK_A + TIM9_CH1 (P1-14), PE6 SAI1_SD_A + TIM9_CH2 (P1-11). PE2/4/5/6 form a complete SAI1 block A with no LCD/SDRAM/gyro/touch/USB use.
- Other free pins: PF6 SAI1_SD_B + TIM10_CH1 (P2-3); PB3 I2S3_CK/TIM2_CH2 (SWO net, SB9 off; JTDO at reset); PB4 TIM3_CH1; PC8 TIM3_CH3 (also SDIO_D0); PA7 TIM3_CH2/TIM14_CH1.
- No complete free I2S port exists: I2S2/I2S3 pins are all used by the board or the user's project.
- Not usable: PA4 DAC_OUT1 = LCD VSYNC (isolation UNKNOWN); PA5 DAC_OUT2 free on board (P2-21) but same pin as ADC12_IN5 and used by the user's repo as the stick VRx ADC input (DAC and stick cannot share it unless the stick moves); PF7/8/9 = gyro SPI; PA1/PA2 gyro INT; PA9/PA10 USART1 VCP; PA8/PC9 touch I2C3.
- Repo-used pins PC11 PC12 PD2 PD4 PD5 PB7 PC3 PC4 have no board function listed.
- Conclusion [assumed, derived]: R8 is answerable. A MAX98357A-class I2S amp can use SAI1 block A on PE4 (FS), PE5 (SCK), PE6 (SD); MCLK on PE2 is free if needed (MAX98357A MCLK need not verified here). PWM audio via TIM9 on PE5/PE6 is the fallback; DAC on PA5 needs the stick ADC moved. Mark R8 resolved-by-datasheet, to be confirmed on the board in S3.

## 7 Unresolved / conflicts
- ESP32 TRM fapb/2 (40 MHz) vs IDF 80 MHz on IO_MUX pins: not reconciled. ESP32 FPU precision UNKNOWN. ESP32 i80/RGB/PSRAM speed UNKNOWN.
- H743 vs H750 FMC rating (100 vs 133 MHz); H7 timer count (22 vs DS table); H7 cache-maintenance text for DMA/LTDC/DMA2D absent from RM0433.
- F429: SRAM1/2/3 split, ADC channel count for ZI, FPU precision UNKNOWN.
- ILI9341 max SPI clock and write-cycle UNKNOWN (check in S1.3).
- RP: PSRAM on RP2040, PIO-USB in official docs, RP2350 overclock rating UNKNOWN.
- SDSPI on both ESP chips UNKNOWN.
- Errata unread (ES0392/H7 and F4). STM32 sources are mirrors, not st.com.
