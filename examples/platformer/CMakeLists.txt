add_executable(pge_platformer main.cpp)
target_link_libraries(pge_platformer PRIVATE pge_core pge_platform_host)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(pge_platformer PRIVATE -Wall -Wextra)
endif()
add_custom_command(TARGET pge_platformer POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:pge_platformer>)
