# Coin Catcher tutorial: one exe per step, each built from the shared main.cpp with that step's game.h.
foreach(step 1 2 3 4 5)
  set(t pge_tutorial_step${step})
  add_executable(${t} main.cpp)
  target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/step${step})
  target_link_libraries(${t} PRIVATE pge_core pge_platform_host)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${t} PRIVATE -Wall -Wextra)
  endif()
  add_custom_command(TARGET ${t} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:${t}>)
  # All steps copy the same DLL into one directory; build them one after another so the copies never race.
  if(step GREATER 1)
    math(EXPR prev "${step} - 1")
    add_dependencies(${t} pge_tutorial_step${prev})
  endif()
endforeach()
