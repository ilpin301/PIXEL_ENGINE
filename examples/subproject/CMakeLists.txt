# Minimal game project that uses PGE through add_subdirectory() (S9.1). Not part of the engine build; see
# docs/user/install.md section 7. It builds the scroller example as the game.
#   host: cmake -S examples/subproject -B build-sub -G Ninja -DSDL2_DIR=<SDL2 cmake dir>
#   F429: cmake -S examples/subproject -B build-sub-f429 -G Ninja "-DCMAKE_TOOLCHAIN_FILE=<PGE root>/cmake/arm-none-eabi.cmake" -DCMAKE_BUILD_TYPE=Release
cmake_minimum_required(VERSION 3.21)
project(pge_subproject LANGUAGES C CXX)

if(CMAKE_CROSSCOMPILING)
  enable_language(ASM)   # Cube startup file
  set(PGE_PLATFORM stm32f429i_disco)
else()
  set(PGE_PLATFORM host)
endif()
get_filename_component(PGE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_subdirectory(${PGE_ROOT} pge)

if(PGE_PLATFORM STREQUAL "host")
  add_executable(demo ${PGE_ROOT}/examples/scroller/main.cpp)
  target_link_libraries(demo PRIVATE pge_core pge_platform_host)
  if(WIN32)
    add_custom_command(TARGET demo POST_BUILD   # SDL2.dll next to the exe
      COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:demo> $<TARGET_FILE_DIR:demo>
      COMMAND_EXPAND_LISTS)
  endif()
else()
  add_executable(demo_f429 ${PGE_ROOT}/examples/f429/scroller_main.cpp)
  target_include_directories(demo_f429 PRIVATE ${PGE_ROOT}/examples/scroller)
  target_link_libraries(demo_f429 PRIVATE pge_core pge_f429_hal)
  pge_f429_firmware(demo_f429 flash_demo_f429)
endif()
