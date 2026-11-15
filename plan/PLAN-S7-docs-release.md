# Part S7 — Documentation and release (Stage 7)
_Three sessions. Index and rules: `PLAN.md`._

## S7.1 User docs
- **Steps:** `README.md`; install per toolchain (CMake host, arm-none-eabi, ESP-IDF); hardware setup for the two Tier A boards (wiring taken from the reference repos and re-verified); API reference from header comments; module docs; examples guide; benchmark summary linking the measured files.
- **Done when:** every public header is covered; every number links to a measured or calculated source.

## S7.2a Porting guide and Tier B skeletons
- **Steps:** Porting guide (the HAL checklist a new board must implement, with the F429 and S3 ports as worked examples; USB HID host input listed as a future item); Tier B skeletons (RP2040 / RP2350, STM32H7, ESP32 classic, generic STM32F4), built where the vendor SDK is installed and labeled untested (or unbuilt) otherwise.
- **Done when:** each Tier B platform has a skeleton and a status line (built / unbuilt, untested).

## S7.2b Wrappers and release
- **Steps:** PlatformIO manifest; STM32CubeIDE import notes; VS Code tasks; third-party notices file (STM32Cube HAL / BSP is BSD-3-Clause, plus any other bundled code); changelog; version tag. Publishing to GitHub only on the user's explicit go.
- **Done when:** a clean checkout builds the host target and both Tier A targets by following the docs only.
