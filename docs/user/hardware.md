# Hardware setup (Tier A boards)

> Summary for later sessions
> - Tier A targets: STM32F429I-DISCO (implemented, board-checked for display, buttons, stick, touch), host (SDL2, no hardware), ESP32-S3 (not implemented).
> - F429: 180 MHz, 240x320 portrait LTDC panel, SDRAM at 0xD0000000, ST-LINK, no UART (debug via the pge_dbg_log ring buffer read with OpenOCD).
> - Input pins come from platform/stm32f429i_disco/input_gpio.c; audio pins (PE4/PE5/PE6) from audio_sai.c.
> - The F429 audio backend is compile-only: nothing was run on a board (STATUS.md S5.5 / S6.2b).
> - The F429 has no storage backend: SAVE / LOAD show NO STORAGE.
> - ESP32-S3 is deferred with session S4.5 (user decision 2026-11-15); nothing is specified for it.
> - Wiring was read from the HAL source files; re-verify against your board before powering external parts.

## 1. STM32F429I-DISCO

### What the board provides

| Item | Value |
|---|---|
| Clock | 180 MHz from the 8 MHz HSE (PLL 8/360/2, overdrive) |
| Screen | 240x320 portrait RGB565 panel on the LTDC |
| Logical size used by the engine | 320x240 landscape (the display HAL transposes); a 240x320 portrait mode also exists |
| External RAM | SDRAM at 0xD0000000; framebuffers FB0 0xD0000000 and FB1 0xD0040000 |
| Programmer | on-board ST-LINK (used by OpenOCD) |
| UART | none used; debug output goes to a RAM ring buffer |
| LEDs | LD3 red = error, LD4 green = ok (used by the bench firmwares) |
| Touch | STMPE811 on I2C3, through the ST BSP |
| Audio codec | none on the board (see the audio section) |

Orientation: default `PGE_F429_ROT=0` is upright (confirmed on the board). `-DPGE_F429_ROT=1` selects the 180-degree-rotated variant.

Wiring was read from the HAL source files; re-verify against your board before powering external parts.

### Input wiring

Source: `platform/stm32f429i_disco/input_gpio.c`. The same table is used by the `pge_f429_input` test firmware (buttons PA0/PC11/PC12/PD2/PD5, stick on PC3/PA5).

| Pin | Function | Active level | Engine bit |
|---|---|---|---|
| PA0 | USER button (on the board) | high (board pulldown) | A |
| PC11 | external button: fire | low (internal pull-up) | A |
| PC12 | external button: use | low (internal pull-up) | B |
| PD2 | external button: previous weapon | low (internal pull-up) | SELECT |
| PD5 | external button: menu | low (internal pull-up) | START |
| PD4 | external button: next weapon | low (internal pull-up) | none (read, unmapped) |
| PB7 | KY-023 stick switch | low (internal pull-up) | none (read, unmapped) |
| PC3 | KY-023 VRy, ADC1_IN13 | analog | horizontal axis |
| PA5 | KY-023 VRx, ADC1_IN5 | analog | vertical axis |

Notes from the source:

- Buttons are wired between the pin and ground (active low). PA0 is the exception: it is the on-board USER button and reads high when pressed.
- PD4 and PB7 are read but not mapped, because there is no free `PGE_BTN_*` bit. `PGE_INPUT_QUIT` is never set on the board.
- Stick: the ADC centre is measured once at init (average of 16 readings per axis), so keep the stick centred while the board starts. The direction bits use a dead zone of 12000 on the +/-32767 scale. Unconnected stick pins float, so the centre and the direction bits are then noise.
- Axis sign: axis[0] > 0 = RIGHT, axis[1] > 0 = DOWN. The X sign was inverted on 2026-11-15 after the platformer play check (stick right was reported as left); Y is unchanged.
- Power and ground pins of the KY-023 are not specified in the HAL source files; check the module and board documentation.
- Touch: the STMPE811 is found at init through the BSP. If it is not found, touch stays disabled and input init still succeeds. Touch is reported in landscape display coordinates (x 0..319, y 0..239). The source comment says the axis orientation of the controller relative to the LCD is to be verified on the board; the S4.3 board check (2026-11-15) passed for buttons, stick and touch.

### Optional audio wiring (MAX98357A)

Source: `platform/stm32f429i_disco/audio_sai.c` and `docs/dev/toolchain.md` (S5.5).

| Board pin | SAI1 signal | MAX98357A pin |
|---|---|---|
| PE4 | FS_A (word clock) | LRC |
| PE5 | SCK_A (bit clock) | BCLK |
| PE6 | SD_A (data) | DIN |

- All three pins are configured as alternate function 6 (SAI1). PE2 (MCLK) is not configured; the MAX98357A needs no master clock.
- Format: SAI1 block A as I2S master transmitter, 16-bit stereo, circular DMA. Supported sample rates in the source: 8000, 11025, 16000, 22050, 32000, 44100 and 48000 Hz.
- The research note (`docs/research/02-hardware-comparison.md`, section 6) lists PE2/PE4/PE5/PE6 as free on the DISC1 board and says to re-check against the board's own user manual before wiring.
- Supply, ground, gain and shutdown pins of the amplifier are not specified in the source files.

Caveat: the audio backend is compile-only. Nothing was run on a board. STATUS.md lists the S5.5 board check as not done: wire the amplifier, flash `pge_f429_audio`, read the AUDIO lines from `pge_dbg_log`, and confirm sound and the absence of underruns or glitches yourself. Flashing replaces the firmware now on the board.

### Storage

Not available. The F429 firmware has no diskio or block device; the storage stubs return an I/O error, so SAVE and LOAD in the sandbox example show `NO STORAGE`. USB mass storage is not implemented. Save and load are verified on the host only.

### Flashing

Prerequisites: ST-LINK USB cable, `arm-none-eabi-gcc`, OpenOCD, CMake and Ninja (see `docs/dev/toolchain.md`).

Configure and build:

```bash
cmake -S . -B build-f429 -G Ninja -DPGE_PLATFORM=stm32f429i_disco -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-f429
```

Flash targets (all need the board; each uses OpenOCD with the ST-LINK):

| Target | Firmware |
|---|---|
| `flash_f429` | `pge_f429_scene` |
| `flash_f429_b1` | `pge_f429_b1` (display variants and micro benchmarks) |
| `flash_f429_input` | `pge_f429_input` (buttons, stick, touch crosshair) |
| `flash_f429_scroller` | `pge_f429_scroller` |
| `flash_f429_platformer` | `pge_f429_platformer` (playable) |
| `flash_f429_platbench` | `pge_f429_platbench` (autoplay bench) |
| `flash_f429_platbench_nobb` | `pge_f429_platbench_nobb` (autoplay bench, no vsync) |
| `flash_f429_sandbox` | `pge_f429_sandbox` (playable) |
| `flash_f429_sandbench` | `pge_f429_sandbench` (autoplay bench) |
| `flash_f429_audio` | `pge_f429_audio` (audio bench, compile-only backend) |

Example:

```bash
cmake --build build-f429 --target flash_f429_sandbox
```

Reading the debug log (there is no UART). The ring buffer `pge_dbg_log` is 32768 bytes and starts with the string `PGELOG1`; the write offset is the uint32 `pge_dbg_pos`. Symbol addresses move with every build, so look them up first:

```bash
arm-none-eabi-nm build-f429/platform/stm32f429i_disco/pge_f429_b1.elf | grep -E "pge_dbg_(log|pos)$"
openocd -f interface/stlink.cfg -c "transport select hla_swd" -f target/stm32f4x.cfg -c "init" -c "mdw <pge_dbg_pos addr>" -c "dump_image build-f429/dump/log.bin <pge_dbg_log addr> 32768" -c "shutdown"
```

In the dump, the text is bytes [8:pos]; if the buffer has wrapped, it is bytes [pos+1:] followed by [8:pos]. Bench firmwares end the log with a `# ... DONE` line and switch LED4 on; LED3 on means an error.

Restoring the original board firmware (backup file path as recorded in `docs/dev/toolchain.md`; adjust it to where your own backup lives):

```bash
openocd -f interface/stlink.cfg -c "transport select hla_swd" -f target/stm32f4x.cfg -c "program F:/____IL_CPP/STM32F429I-DISCO/backup/flash_backup_0x08000000_2MB.bin 0x08000000 verify reset exit"
```

## 2. Host (no hardware needed)

The host platform runs in an SDL2 window on the development PC. No board, wiring or programmer is required; SDL2 is the only extra requirement (see `docs/dev/toolchain.md`).

Keyboard map (`platform/host/input_sdl.cpp`):

| Key | Engine input |
|---|---|
| Up arrow, W | UP |
| Down arrow, S | DOWN |
| Left arrow, A | LEFT |
| Right arrow, D | RIGHT |
| Z, J | A |
| X, K | B |
| Enter | START |
| Backspace, Right Shift | SELECT |
| Escape (or closing the window) | quit |
| Left mouse button | touch at the pointer position |

Direction keys also drive axes 0 and 1 at full deflection. An example may switch W/A/S/D from buttons to a second aim stick (axes 2 and 3); the arrow keys then keep the buttons and axes 0 and 1.

## 3. ESP32-S3

Not implemented: deferred with session S4.5 (user decision 2026-11-15). platform/esp32s3 is an empty placeholder; ESP-IDF is not installed on the dev machine.
