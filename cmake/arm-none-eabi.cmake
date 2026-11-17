# Cross toolchain for Cortex-M4F (STM32F429) by default; a preset PGE_MCU_FLAGS (tier B) overrides the CPU. Use: -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_COMPILER   arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY      arm-none-eabi-objcopy)
set(CMAKE_SIZE         arm-none-eabi-size)

# Same ABI flags for every language, so core/ and the Cube code link together.
if(NOT DEFINED PGE_MCU_FLAGS)
  set(PGE_MCU_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")
endif()
set(CMAKE_C_FLAGS_INIT   "${PGE_MCU_FLAGS} -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_INIT "${PGE_MCU_FLAGS} -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -fno-threadsafe-statics")
set(CMAKE_ASM_FLAGS_INIT "${PGE_MCU_FLAGS} -x assembler-with-cpp")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${PGE_MCU_FLAGS} --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections")
set(CMAKE_C_FLAGS_RELEASE_INIT   "-O2 -g")
set(CMAKE_CXX_FLAGS_RELEASE_INIT "-O2 -g")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
