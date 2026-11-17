add_library(pge_tierb_hal STATIC hal_stub.c)
target_include_directories(pge_tierb_hal PUBLIC ${CMAKE_SOURCE_DIR}/hal/include ${PGE_PROFILE_DIR})
target_compile_options(pge_tierb_hal PRIVATE -Wall -Wextra)
