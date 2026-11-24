# STM32F429I-DISCO platform (S3.2a): Cube HAL + BSP + our display/time HAL, and the scene firmware.
set(PGE_CUBE_DIR "F:/____IL_CPP/STM32F429I-DISCO/STM32CubeF4" CACHE PATH "STM32CubeF4 checkout (not vendored)")
set(HAL ${PGE_CUBE_DIR}/Drivers/STM32F4xx_HAL_Driver)
set(BSP ${PGE_CUBE_DIR}/Drivers/BSP/STM32F429I-Discovery)
set(CMSIS_DEV ${PGE_CUBE_DIR}/Drivers/CMSIS/Device/ST/STM32F4xx)
if(NOT EXISTS ${HAL}/Src/stm32f4xx_hal.c)
  message(FATAL_ERROR "PGE_CUBE_DIR does not point at STM32CubeF4: ${PGE_CUBE_DIR}")
endif()

set(HAL_MODULES hal hal_adc hal_adc_ex hal_cortex hal_dma hal_dma2d hal_flash hal_flash_ex hal_gpio hal_i2c hal_ltdc hal_ltdc_ex
                hal_pwr hal_pwr_ex hal_rcc hal_rcc_ex hal_sai hal_sai_ex hal_sdram hal_spi ll_fmc hal_hcd ll_usb)
set(HAL_SOURCES)
foreach(m ${HAL_MODULES})
  list(APPEND HAL_SOURCES ${HAL}/Src/stm32f4xx_${m}.c)
endforeach()

set(PGE_F429_OWN board.c display_ltdc.c input_gpio.c audio_sai.c)
add_library(pge_f429_hal STATIC
  ${HAL_SOURCES}
  ${BSP}/stm32f429i_discovery.c
  ${BSP}/stm32f429i_discovery_lcd.c   # #includes Utilities/Fonts/font*.c itself
  ${BSP}/stm32f429i_discovery_sdram.c
  ${BSP}/stm32f429i_discovery_ts.c    # touch (S4.3); needs only IOE_* from stm32f429i_discovery.c, not _io.c
  ${PGE_CUBE_DIR}/Drivers/BSP/Components/stmpe811/stmpe811.c
  ${PGE_CUBE_DIR}/Drivers/BSP/Components/ili9341/ili9341.c
  ${CMSIS_DEV}/Source/Templates/system_stm32f4xx.c
  ${CMSIS_DEV}/Source/Templates/gcc/startup_stm32f429xx.s
  ${PGE_F429_OWN})
set_source_files_properties(${PGE_F429_OWN} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")   # Cube stays unwarned
set(PGE_F429_ROT 0 CACHE STRING "landscape rotation: 1 = panelX 239-y/panelY x, 0 = panelX y/panelY 319-x")
set(PGE_F429_BACKBUF 0 CACHE STRING "scene firmware: 1 = vsync back buffer (FB1) in pge_f429_scene")
target_compile_definitions(pge_f429_hal PUBLIC STM32F429xx USE_HAL_DRIVER PGE_F429_ROT=${PGE_F429_ROT}
                           PGE_F429_BACKBUF=${PGE_F429_BACKBUF})
target_include_directories(pge_f429_hal PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}   # board.h, stm32f4xx_hal_conf.h
  ${PGE_CUBE_DIR}/Drivers/CMSIS/Include ${CMSIS_DEV}/Include ${HAL}/Inc ${BSP}
  ${PGE_CUBE_DIR}/Drivers/BSP/Components/Common ${PGE_CUBE_DIR}/Drivers/BSP/Components/ili9341
  ${PGE_CUBE_DIR}/Drivers/BSP/Components/stmpe811 ${PGE_CUBE_DIR}/Utilities/Fonts ${PROJECT_SOURCE_DIR}/hal/include)

# USB stick saves (S8.1): ST USB Host library (MSC) + FatFs R0.12c from the Cube checkout + drivers/storage_fatfs. Linked only into pge_f429_sandbox; Cube/FatFs sources stay unwarned.
set(USBH ${PGE_CUBE_DIR}/Middlewares/ST/STM32_USB_Host_Library)
set(FATFS_SRC ${PGE_CUBE_DIR}/Middlewares/Third_Party/FatFs/src)
add_library(pge_f429_usb STATIC
  ${USBH}/Core/Src/usbh_core.c ${USBH}/Core/Src/usbh_ctlreq.c ${USBH}/Core/Src/usbh_ioreq.c ${USBH}/Core/Src/usbh_pipes.c
  ${USBH}/Class/MSC/Src/usbh_msc.c ${USBH}/Class/MSC/Src/usbh_msc_bot.c ${USBH}/Class/MSC/Src/usbh_msc_scsi.c
  ${FATFS_SRC}/ff.c ${FATFS_SRC}/option/unicode.c
  usb_msc.c ${PROJECT_SOURCE_DIR}/drivers/storage_fatfs/storage_fatfs.cpp)   # not Cube's diskio.c / ff_gen_drv.c
set_source_files_properties(usb_msc.c ${PROJECT_SOURCE_DIR}/drivers/storage_fatfs/storage_fatfs.cpp PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
target_include_directories(pge_f429_usb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${USBH}/Core/Inc ${USBH}/Class/MSC/Inc ${FATFS_SRC}
                           ${PROJECT_SOURCE_DIR}/drivers/storage_fatfs)
target_link_libraries(pge_f429_usb PUBLIC pge_f429_hal)

# pge_f429_firmware() lives in cmake/pge_f429_firmware.cmake; a game project can call it too
include(${PROJECT_SOURCE_DIR}/cmake/pge_f429_firmware.cmake)

if(PGE_BUILD_EXAMPLES)   # engine example firmwares; a game project gets only the libraries above
# core first in link order: it needs the display/time HAL symbols
add_executable(pge_f429_scene ${PROJECT_SOURCE_DIR}/examples/f429/main.cpp)
target_include_directories(pge_f429_scene PRIVATE ${PROJECT_SOURCE_DIR}/examples/proto)
target_link_libraries(pge_f429_scene PRIVATE pge_core pge_f429_hal)
add_dependencies(pge_f429_scene pge_b1)   # compile-check b1.cpp for ARM
pge_f429_firmware(pge_f429_scene flash_f429)

add_executable(pge_f429_b1 ${PROJECT_SOURCE_DIR}/examples/f429/b1_main.cpp)
target_link_libraries(pge_f429_b1 PRIVATE pge_b1 pge_core pge_f429_hal)   # pge_b1 pulls pge_core; core again after b1
pge_f429_firmware(pge_f429_b1 flash_f429_b1)

add_executable(pge_f429_input ${PROJECT_SOURCE_DIR}/examples/f429/input_main.cpp)
target_link_libraries(pge_f429_input PRIVATE pge_core pge_f429_hal)
pge_f429_firmware(pge_f429_input flash_f429_input)

add_executable(pge_f429_scroller ${PROJECT_SOURCE_DIR}/examples/f429/scroller_main.cpp)
target_include_directories(pge_f429_scroller PRIVATE ${PROJECT_SOURCE_DIR}/examples/scroller)
target_link_libraries(pge_f429_scroller PRIVATE pge_core pge_f429_hal)
pge_f429_firmware(pge_f429_scroller flash_f429_scroller)

add_executable(pge_f429_audio ${PROJECT_SOURCE_DIR}/examples/f429/audio_main.cpp)
target_link_libraries(pge_f429_audio PRIVATE pge_core pge_f429_hal)
pge_f429_firmware(pge_f429_audio flash_f429_audio)

# One main source, two compile-time switches: bench (scripted bot + log summary) and the vsync back buffer.
function(pge_f429_platformer tgt flash_tgt bench backbuf)
  add_executable(${tgt} ${PROJECT_SOURCE_DIR}/examples/f429/platformer_main.cpp)
  target_include_directories(${tgt} PRIVATE ${PROJECT_SOURCE_DIR}/examples/platformer)
  target_compile_definitions(${tgt} PRIVATE PGE_PLATFORMER_BENCH=${bench} PGE_PLAT_BACKBUF=${backbuf})
  target_link_libraries(${tgt} PRIVATE pge_core pge_f429_hal)
  pge_f429_firmware(${tgt} ${flash_tgt})
endfunction()
pge_f429_platformer(pge_f429_platformer flash_f429_platformer 0 1)
pge_f429_platformer(pge_f429_platbench flash_f429_platbench 1 1)
pge_f429_platformer(pge_f429_platbench_nobb flash_f429_platbench_nobb 1 0)

# Planet memory check (S8.2): 4x4 world, 16 cache slots in the init arena, worst-case change log in SDRAM (.sdram NOLOAD).
add_executable(pge_f429_planetmem ${PROJECT_SOURCE_DIR}/examples/f429/planetmem_main.cpp)
target_link_libraries(pge_f429_planetmem PRIVATE pge_core pge_f429_hal)
pge_f429_firmware(pge_f429_planetmem flash_f429_planetmem)

# Orientation viewer (S8.4b): the test picture in 4 camera orientations, auto-cycling.
add_executable(pge_f429_orient ${PROJECT_SOURCE_DIR}/examples/f429/orient_main.cpp)
target_link_libraries(pge_f429_orient PRIVATE pge_core pge_f429_hal)
pge_f429_firmware(pge_f429_orient flash_f429_orient)

# Sandbox example (S6.2b): one main source, compile-time switches SANDBOX_BENCH (autoplay + log summary), SANDBOX_BACKBUF and
# SANDBOX_USB (USB stick saves, 168 MHz).
function(pge_f429_sandbox tgt flash_tgt bench backbuf usb)
  add_executable(${tgt} ${PROJECT_SOURCE_DIR}/examples/f429/sandbox_main.cpp)
  target_include_directories(${tgt} PRIVATE ${PROJECT_SOURCE_DIR}/examples/sandbox)
  target_compile_definitions(${tgt} PRIVATE SANDBOX_BENCH=${bench} SANDBOX_BACKBUF=${backbuf} SANDBOX_USB=${usb})
  if(usb)
    target_link_libraries(${tgt} PRIVATE pge_core pge_f429_usb pge_f429_hal)
  else()
    target_link_libraries(${tgt} PRIVATE pge_core pge_f429_hal)
  endif()
  pge_f429_firmware(${tgt} ${flash_tgt})
endfunction()
pge_f429_sandbox(pge_f429_sandbox flash_f429_sandbox 0 1 1)
# bench keeps the storage stubs and 180 MHz so stage6.md numbers stay comparable
pge_f429_sandbox(pge_f429_sandbench flash_f429_sandbench 1 1 0)

# Planet example (S8.6b): one main source, compile-time switches PLANET_BENCH (autoplay + log summary), PLANET_USB (USB
# stick saves, 168 MHz) and PLANET_ROTATE (the world turns with the planet); the vsync back buffer is always on. The
# change log lives in SDRAM (.sdram), so no switch for it. cmake -DPLANET_CORE=N sets the radius in tiles of the unbreakable
# planet core (default: PlanetGame::kCoreDefault) for all planet targets.
function(pge_f429_planet tgt flash_tgt bench usb rotate)
  add_executable(${tgt} ${PROJECT_SOURCE_DIR}/examples/f429/planet_main.cpp)
  target_include_directories(${tgt} PRIVATE ${PROJECT_SOURCE_DIR}/examples/planet)
  target_compile_definitions(${tgt} PRIVATE PLANET_BENCH=${bench} PLANET_USB=${usb} PLANET_ROTATE=${rotate})
  if(DEFINED PLANET_CORE)
    target_compile_definitions(${tgt} PRIVATE PLANET_CORE=${PLANET_CORE})
  endif()
  if(usb)
    target_link_libraries(${tgt} PRIVATE pge_core pge_f429_usb pge_f429_hal)
  else()
    target_link_libraries(${tgt} PRIVATE pge_core pge_f429_hal)
  endif()
  pge_f429_firmware(${tgt} ${flash_tgt})
endfunction()
pge_f429_planet(pge_f429_planet flash_f429_planet 0 1 0)
pge_f429_planet(pge_f429_planet_rot flash_f429_planet_rot 0 1 1)
# bench keeps the storage stubs and 180 MHz, like the sandbench
pge_f429_planet(pge_f429_planetbench flash_f429_planetbench 1 0 0)
pge_f429_planet(pge_f429_planetbench_rot flash_f429_planetbench_rot 1 0 1)
endif()
