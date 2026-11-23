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
  ${PGE_CUBE_DIR}/Drivers/BSP/Components/stmpe811 ${PGE_CUBE_DIR}/Utilities/Fonts ${CMAKE_SOURCE_DIR}/hal/include)

# USB stick saves (S8.1): ST USB Host library (MSC) + FatFs R0.12c from the Cube checkout + drivers/storage_fatfs. Linked only into pge_f429_sandbox; Cube/FatFs sources stay unwarned.
set(USBH ${PGE_CUBE_DIR}/Middlewares/ST/STM32_USB_Host_Library)
set(FATFS_SRC ${PGE_CUBE_DIR}/Middlewares/Third_Party/FatFs/src)
add_library(pge_f429_usb STATIC
  ${USBH}/Core/Src/usbh_core.c ${USBH}/Core/Src/usbh_ctlreq.c ${USBH}/Core/Src/usbh_ioreq.c ${USBH}/Core/Src/usbh_pipes.c
  ${USBH}/Class/MSC/Src/usbh_msc.c ${USBH}/Class/MSC/Src/usbh_msc_bot.c ${USBH}/Class/MSC/Src/usbh_msc_scsi.c
  ${FATFS_SRC}/ff.c ${FATFS_SRC}/option/unicode.c
  usb_msc.c ${CMAKE_SOURCE_DIR}/drivers/storage_fatfs/storage_fatfs.cpp)   # not Cube's diskio.c / ff_gen_drv.c
set_source_files_properties(usb_msc.c ${CMAKE_SOURCE_DIR}/drivers/storage_fatfs/storage_fatfs.cpp PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
target_include_directories(pge_f429_usb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${USBH}/Core/Inc ${USBH}/Class/MSC/Inc ${FATFS_SRC}
                           ${CMAKE_SOURCE_DIR}/drivers/storage_fatfs)
target_link_libraries(pge_f429_usb PUBLIC pge_f429_hal)

set(LD ${CMAKE_CURRENT_SOURCE_DIR}/STM32F429ZITX_FLASH.ld)
# One firmware image: elf + map + bin + hex + size, plus a flash_<name> target (openocd + ST-LINK, needs the board).
function(pge_f429_firmware tgt flash_tgt)
  target_compile_options(${tgt} PRIVATE -Wall -Wextra)
  # --undefined pulls the startup object (vector table) out of the archive
  target_link_options(${tgt} PRIVATE -T${LD} -Wl,--undefined=Reset_Handler -Wl,-Map=$<TARGET_FILE_DIR:${tgt}>/${tgt}.map)
  set_target_properties(${tgt} PROPERTIES SUFFIX .elf LINK_DEPENDS ${LD})
  add_custom_command(TARGET ${tgt} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${tgt}> $<TARGET_FILE_DIR:${tgt}>/${tgt}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${tgt}> $<TARGET_FILE_DIR:${tgt}>/${tgt}.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${tgt}>)
  add_custom_target(${flash_tgt}
    COMMAND openocd -f interface/stlink.cfg -c "transport select hla_swd" -f target/stm32f4x.cfg
            -c "program $<TARGET_FILE:${tgt}> verify reset exit"
    DEPENDS ${tgt}
    USES_TERMINAL)
endfunction()

# core first in link order: it needs the display/time HAL symbols
add_executable(pge_f429_scene ${CMAKE_SOURCE_DIR}/examples/f429/main.cpp)
target_include_directories(pge_f429_scene PRIVATE ${CMAKE_SOURCE_DIR}/examples/proto)
target_link_libraries(pge_f429_scene PRIVATE pge_core pge_f429_hal)
add_dependencies(pge_f429_scene pge_b1)   # compile-check b1.cpp for ARM
pge_f429_firmware(pge_f429_scene flash_f429)

add_executable(pge_f429_b1 ${CMAKE_SOURCE_DIR}/examples/f429/b1_main.cpp)
target_link_libraries(pge_f429_b1 PRIVATE pge_b1 pge_core pge_f429_hal)   # pge_b1 pulls pge_core; core again after b1
pge_f429_firmware(pge_f429_b1 flash_f429_b1)

add_executable(pge_f429_input ${CMAKE_SOURCE_DIR}/examples/f429/input_main.cpp)
target_link_libraries(pge_f429_input PRIVATE pge_core pge_f429_hal)
pge_f429_firmware(pge_f429_input flash_f429_input)

add_executable(pge_f429_scroller ${CMAKE_SOURCE_DIR}/examples/f429/scroller_main.cpp)
target_include_directories(pge_f429_scroller PRIVATE ${CMAKE_SOURCE_DIR}/examples/scroller)
target_link_libraries(pge_f429_scroller PRIVATE pge_core pge_f429_hal)
pge_f429_firmware(pge_f429_scroller flash_f429_scroller)

add_executable(pge_f429_audio ${CMAKE_SOURCE_DIR}/examples/f429/audio_main.cpp)
target_link_libraries(pge_f429_audio PRIVATE pge_core pge_f429_hal)
pge_f429_firmware(pge_f429_audio flash_f429_audio)

# One main source, two compile-time switches: bench (scripted bot + log summary) and the vsync back buffer.
function(pge_f429_platformer tgt flash_tgt bench backbuf)
  add_executable(${tgt} ${CMAKE_SOURCE_DIR}/examples/f429/platformer_main.cpp)
  target_include_directories(${tgt} PRIVATE ${CMAKE_SOURCE_DIR}/examples/platformer)
  target_compile_definitions(${tgt} PRIVATE PGE_PLATFORMER_BENCH=${bench} PGE_PLAT_BACKBUF=${backbuf})
  target_link_libraries(${tgt} PRIVATE pge_core pge_f429_hal)
  pge_f429_firmware(${tgt} ${flash_tgt})
endfunction()
pge_f429_platformer(pge_f429_platformer flash_f429_platformer 0 1)
pge_f429_platformer(pge_f429_platbench flash_f429_platbench 1 1)
pge_f429_platformer(pge_f429_platbench_nobb flash_f429_platbench_nobb 1 0)

# Planet memory check (S8.2): 4x4 world, 16 cache slots in the init arena, worst-case change log in SDRAM (.sdram NOLOAD).
add_executable(pge_f429_planetmem ${CMAKE_SOURCE_DIR}/examples/f429/planetmem_main.cpp)
target_link_libraries(pge_f429_planetmem PRIVATE pge_core pge_f429_hal)
pge_f429_firmware(pge_f429_planetmem flash_f429_planetmem)

# Orientation viewer (S8.4b): the test picture in 4 camera orientations, auto-cycling.
add_executable(pge_f429_orient ${CMAKE_SOURCE_DIR}/examples/f429/orient_main.cpp)
target_link_libraries(pge_f429_orient PRIVATE pge_core pge_f429_hal)
pge_f429_firmware(pge_f429_orient flash_f429_orient)

# Sandbox example (S6.2b): one main source, compile-time switches SANDBOX_BENCH (autoplay + log summary), SANDBOX_BACKBUF and
# SANDBOX_USB (USB stick saves, 168 MHz).
function(pge_f429_sandbox tgt flash_tgt bench backbuf usb)
  add_executable(${tgt} ${CMAKE_SOURCE_DIR}/examples/f429/sandbox_main.cpp)
  target_include_directories(${tgt} PRIVATE ${CMAKE_SOURCE_DIR}/examples/sandbox)
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
