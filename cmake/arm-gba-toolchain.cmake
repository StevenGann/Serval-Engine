# CMake toolchain file for cross-compiling to the Game Boy Advance with any
# arm-none-eabi GCC (ARM GNU Toolchain, devkitARM, ...).
#
# The compiler is found on PATH, or under $ARM_GNU_TOOLCHAIN/bin if set.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(_serval_toolchain_hints "")
if(DEFINED ENV{ARM_GNU_TOOLCHAIN})
    list(APPEND _serval_toolchain_hints "$ENV{ARM_GNU_TOOLCHAIN}/bin")
endif()

find_program(CMAKE_C_COMPILER arm-none-eabi-gcc HINTS ${_serval_toolchain_hints} REQUIRED)
set(CMAKE_ASM_COMPILER "${CMAKE_C_COMPILER}")
cmake_path(GET CMAKE_C_COMPILER PARENT_PATH _serval_toolchain_bin)
find_program(SERVAL_OBJCOPY arm-none-eabi-objcopy HINTS "${_serval_toolchain_bin}" REQUIRED)

# There is no startup code until the engine's crt0 is linked, so compiler
# checks must not try to link an executable.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ARM7TDMI, Thumb by default. Hot code opts into ARM mode per function
# (SERVAL_IWRAM_CODE). These flags select the armv4t Thumb multilib of libgcc.
set(CMAKE_C_FLAGS_INIT "-mcpu=arm7tdmi -mtune=arm7tdmi -mthumb -mfloat-abi=soft")
set(CMAKE_ASM_FLAGS_INIT "-mcpu=arm7tdmi -mfloat-abi=soft")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
