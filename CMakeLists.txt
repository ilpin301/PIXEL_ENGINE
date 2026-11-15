cmake_minimum_required(VERSION 3.20)

set(PGE_PLATFORM host CACHE STRING "host | stm32f429i_disco")
if(PGE_PLATFORM STREQUAL "stm32f429i_disco")
  set(PGE_LANGS CXX C ASM)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
  endif()
else()
  set(PGE_LANGS CXX)
endif()
project(pge LANGUAGES ${PGE_LANGS})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(core)

if(PGE_PLATFORM STREQUAL "stm32f429i_disco")
  # pge_b1 only; its host exe does not build for ARM, hence EXCLUDE_FROM_ALL (the scene exe depends on pge_b1).
  add_subdirectory(bench/b1 EXCLUDE_FROM_ALL)
  add_subdirectory(platform/stm32f429i_disco)
else()
  option(PGE_BUILD_TESTS "Build host unit tests" ON)

  find_package(SDL2 CONFIG QUIET)
  add_subdirectory(platform/host)
  if(SDL2_FOUND)
    add_subdirectory(examples/proto)
  else()
    message(STATUS "SDL2 not found: host window demo skipped")
  endif()

  add_subdirectory(bench/b1)

  if(PGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
  endif()
endif()
