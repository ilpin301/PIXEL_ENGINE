add_library(pge_host_common STATIC host_common.cpp)
target_include_directories(pge_host_common PUBLIC ${CMAKE_SOURCE_DIR}/hal/include ${CMAKE_CURRENT_SOURCE_DIR})

add_library(pge_host_storage STATIC storage_file.cpp)
target_link_libraries(pge_host_storage PUBLIC pge_host_common)

add_library(pge_platform_host_null STATIC display_null.cpp input_null.cpp audio_null.cpp)
target_link_libraries(pge_platform_host_null PUBLIC pge_host_common pge_host_storage)

set(PGE_HOST_TARGETS pge_host_common pge_host_storage pge_platform_host_null)
if(TARGET SDL2::SDL2)
  add_library(pge_platform_host STATIC display_sdl.cpp input_sdl.cpp audio_sdl.cpp)
  target_link_libraries(pge_platform_host PUBLIC SDL2::SDL2 pge_host_common pge_host_storage)
  list(APPEND PGE_HOST_TARGETS pge_platform_host)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  foreach(t ${PGE_HOST_TARGETS})
    target_compile_options(${t} PRIVATE -Wall -Wextra)
  endforeach()
endif()
