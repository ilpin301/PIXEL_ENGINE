add_library(pge_b1 STATIC b1.cpp)
target_include_directories(pge_b1 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pge_b1 PUBLIC pge_core)

add_executable(pge_b1_host main_host.cpp)
target_link_libraries(pge_b1_host PRIVATE pge_b1 pge_platform_host_null)
target_compile_definitions(pge_b1_host PRIVATE PGE_BUILD_TYPE="$<CONFIG>")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(pge_b1 PRIVATE -Wall -Wextra)
  target_compile_options(pge_b1_host PRIVATE -Wall -Wextra)
endif()
