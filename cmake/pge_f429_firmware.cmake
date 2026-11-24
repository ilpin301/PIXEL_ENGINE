# pge_f429_firmware(<target> <flash target>) turns an STM32F429I-DISCO executable into a firmware image: linker script,
# .map, post-build .bin / .hex / size listing, and a flash target (openocd + ST-LINK, needs the board).
# Included by platform/stm32f429i_disco (S9.1); a game that add_subdirectory()s PGE can call it for its own executables.
include_guard(GLOBAL)

function(pge_f429_firmware tgt flash_tgt)
  # resolved from this file, so the path is right whatever directory calls the function
  get_filename_component(ld "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../platform/stm32f429i_disco/STM32F429ZITX_FLASH.ld" ABSOLUTE)
  target_compile_options(${tgt} PRIVATE -Wall -Wextra)
  # --undefined pulls the startup object (vector table) out of the archive
  target_link_options(${tgt} PRIVATE -T${ld} -Wl,--undefined=Reset_Handler -Wl,-Map=$<TARGET_FILE_DIR:${tgt}>/${tgt}.map)
  set_target_properties(${tgt} PROPERTIES SUFFIX .elf LINK_DEPENDS ${ld})
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
