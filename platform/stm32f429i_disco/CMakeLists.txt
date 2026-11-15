# STM32F429I-DISCO platform (S3.2a): Cube HAL + BSP + our display/time HAL, and the scene firmware.
set(PGE_CUBE_DIR "F:/____IL_CPP/STM32F429I-DISCO/STM32CubeF4" CACHE PATH "STM32CubeF4 checkout (not vendored)")
set(HAL ${PGE_CUBE_DIR}/Drivers/STM32F4xx_HAL_Driver)
set(BSP ${PGE_CUBE_DIR}/Drivers/BSP/STM32F429I-Discovery)
set(CMSIS_DEV ${PGE_CUBE_DIR}/Drivers/CMSIS/Device/ST/STM32F4xx)
if(NOT EXISTS ${HAL}/Src/stm32f4xx_hal.c)
  message(FATAL_ERROR "PGE_CUBE_DIR does not point at STM32CubeF4: ${PGE_CUBE_DIR}")
endif()

set(HAL_MODULES hal hal_cortex hal_dma hal_dma2d hal_flash hal_flash_ex hal_gpio hal_i2c hal_ltdc hal_ltdc_ex
                hal_pwr hal_pwr_ex hal_rcc hal_rcc_ex hal_sdram hal_spi ll_fmc)
set(HAL_SOURCES)
foreach(m ${HAL_MODULES})
  list(APPEND HAL_SOURCES ${HAL}/Src/stm32f4xx_${m}.c)
endforeach()

set(PGE_F429_OWN board.c display_ltdc.c)
add_library(pge_f429_hal STATIC
  ${HAL_SOURCES}
  ${BSP}/stm32f429i_discovery.c
  ${BSP}/stm32f429i_discovery_lcd.c   # #includes Utilities/Fonts/font*.c itself
  ${BSP}/stm32f429i_discovery_sdram.c
  ${PGE_CUBE_DIR}/Drivers/BSP/Components/ili9341/ili9341.c
  ${CMSIS_DEV}/Source/Templates/system_stm32f4xx.c
  ${CMSIS_DEV}/Source/Templates/gcc/startup_stm32f429xx.s
  ${PGE_F429_OWN})
set_source_files_properties(${PGE_F429_OWN} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")   # Cube stays unwarned
set(PGE_F429_ROT 0 CACHE STRING "landscape rotation: 1 = panelX 239-y/panelY x, 0 = panelX y/panelY 319-x")
target_compile_definitions(pge_f429_hal PUBLIC STM32F429xx USE_HAL_DRIVER PGE_F429_ROT=${PGE_F429_ROT})
target_include_directories(pge_f429_hal PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}   # board.h, stm32f4xx_hal_conf.h
  ${PGE_CUBE_DIR}/Drivers/CMSIS/Include ${CMSIS_DEV}/Include ${HAL}/Inc ${BSP}
  ${PGE_CUBE_DIR}/Drivers/BSP/Components/Common ${PGE_CUBE_DIR}/Drivers/BSP/Components/ili9341
  ${PGE_CUBE_DIR}/Utilities/Fonts ${CMAKE_SOURCE_DIR}/hal/include)

add_executable(pge_f429_scene ${CMAKE_SOURCE_DIR}/examples/f429/main.cpp)
target_include_directories(pge_f429_scene PRIVATE ${CMAKE_SOURCE_DIR}/examples/proto)
target_compile_options(pge_f429_scene PRIVATE -Wall -Wextra)
target_link_libraries(pge_f429_scene PRIVATE pge_core pge_f429_hal)   # core first: it needs the display/time HAL symbols
add_dependencies(pge_f429_scene pge_b1)   # compile-check b1.cpp for ARM; not linked

set(LD ${CMAKE_CURRENT_SOURCE_DIR}/STM32F429ZITX_FLASH.ld)
# --undefined pulls the startup object (vector table) out of the archive
target_link_options(pge_f429_scene PRIVATE -T${LD} -Wl,--undefined=Reset_Handler -Wl,-Map=$<TARGET_FILE_DIR:pge_f429_scene>/pge_f429_scene.map)
set_target_properties(pge_f429_scene PROPERTIES SUFFIX .elf LINK_DEPENDS ${LD})

add_custom_command(TARGET pge_f429_scene POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:pge_f429_scene> $<TARGET_FILE_DIR:pge_f429_scene>/pge_f429_scene.bin
  COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:pge_f429_scene> $<TARGET_FILE_DIR:pge_f429_scene>/pge_f429_scene.hex
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:pge_f429_scene>)

add_custom_target(flash_f429
  COMMAND openocd -f interface/stlink.cfg -c "transport select hla_swd" -f target/stm32f4x.cfg
          -c "program $<TARGET_FILE:pge_f429_scene> verify reset exit"
  DEPENDS pge_f429_scene
  USES_TERMINAL)
