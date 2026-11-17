# Tier B platform skeletons

Purpose: compile-check the engine core for other MCUs without any vendor SDK, and give a stub HAL to copy
when starting a real port. Tier B means untested: no board, no vendor SDK, nothing is linked or run.

The profile and logical size per platform are ASSUMPTIONS taken from docs/architecture/01-overview.md
section 6; they are written to a generated `pge_profile.h` by `cmake/tier_b.cmake`. Change the table there
if the real board differs.

## Status

| Platform | CPU / flags | Assumed profile | Build status | Tested |
|---|---|---|---|---|
| rp2040 | `-mcpu=cortex-m0plus -mthumb -mfloat-abi=soft` | SMALL 160x128 | core + stub HAL compile with arm-none-eabi-g++ 10.3.1 (no warnings), not linked, no board, no vendor SDK | no |
| rp2350 | `-mcpu=cortex-m33 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard` | MEDIUM 320x240 | core + stub HAL compile with arm-none-eabi-g++ 10.3.1 (no warnings), not linked, no board, no vendor SDK | no |
| stm32h7 | `-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard` | LARGE 320x240 | core + stub HAL compile with arm-none-eabi-g++ 10.3.1 (no warnings), not linked, no board, no vendor SDK | no |
| stm32f4_generic | `-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard` | SMALL 160x128 | core + stub HAL compile with arm-none-eabi-g++ 10.3.1 (no warnings), not linked, no board, no vendor SDK | no |
| esp32 | none preset (Xtensa / ESP-IDF toolchain file supplied by you) | MEDIUM 320x240 | unbuilt (no Xtensa toolchain / ESP-IDF on the dev machine) | no |

## Build

Needs `arm-none-eabi-gcc/g++` and Ninja on PATH. Replace `<name>` with rp2040, rp2350, stm32h7 or stm32f4_generic:

```
cmake -S . -B build-tb-<name> -G Ninja -DPGE_PLATFORM=<name> "-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake"
cmake --build build-tb-<name>
```

Quote the toolchain argument in PowerShell (unquoted, it is split at the dot).

This builds `pge_core` and `pge_tierb_hal` (static libraries only; no tests, examples or SDL).

esp32: configure with your own toolchain file, e.g.
`cmake -S . -B build-tb-esp32 -G Ninja -DPGE_PLATFORM=esp32 -DCMAKE_TOOLCHAIN_FILE=<your Xtensa toolchain file>`.
Not tried here.

## Turning a skeleton into a port

1. Copy `platform/tier_b/hal_stub.c` to `platform/<board>/` and rename it as you like.
2. Add `platform/<board>/CMakeLists.txt` and `pge_profile.h` with the real profile and size, modelled on
   `platform/stm32f429i_disco` (startup code, linker script and vendor SDK go there, not in core).
3. Implement every `pge_hal_*` function against the board; the contracts are in `hal/include/pge/hal/*.h`
   and `docs/user/api-hal.md` (the main reference is the porting guide, docs/user/porting.md).
4. Wire the new directory into the root `CMakeLists.txt` the way the F429 branch is, and drop the platform
   from the tier B table in `cmake/tier_b.cmake`.
