cmake_minimum_required(VERSION 3.25)

# serval.json is the single source of truth for the engine version.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/serval.json" _serval_manifest)
string(JSON SERVAL_VERSION GET "${_serval_manifest}" version)

project(ServalEngine
    VERSION ${SERVAL_VERSION}
    DESCRIPTION "Game Boy Advance game runtime"
    HOMEPAGE_URL "https://github.com/StevenGann/Serval-Engine"
    LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON) # GNU C: attributes and inline asm
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_SYSTEM_NAME STREQUAL "Generic" AND CMAKE_SYSTEM_PROCESSOR STREQUAL "arm")
    set(SERVAL_TARGET_GBA ON)
else()
    set(SERVAL_TARGET_GBA OFF)
endif()

option(SERVAL_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(SERVAL_BUILD_TESTS "Build the test suites" ${PROJECT_IS_TOP_LEVEL})
option(SERVAL_BUILD_EXAMPLES "Build the example ROMs" ${PROJECT_IS_TOP_LEVEL})

include(cmake/Serval.cmake)

# Platform-neutral engine code. Builds for the GBA and for the host, so its
# logic can be unit tested natively. See docs/platforms.md.
add_library(serval_portable STATIC
    src/core/random.c
    src/core/text_format.c
    src/ecs/ecs.c
)
target_include_directories(serval_portable PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(serval_portable PRIVATE serval_warnings)

# Debug builds report API misuse as warnings (src/core/warn.h). Public, so
# games can key their own debug code off SERVAL_DEBUG too.
target_compile_definitions(serval_portable PUBLIC $<$<CONFIG:Debug,RelWithDebInfo>:SERVAL_DEBUG>)

if(NOT SERVAL_TARGET_GBA)
    target_sources(serval_portable PRIVATE src/host/debug.c)
endif()

if(SERVAL_TARGET_GBA)
    enable_language(ASM)
    target_compile_definitions(serval_portable PUBLIC SERVAL_GBA)

    add_subdirectory(third_party)

    add_library(serval STATIC
        src/gba/core.c
        src/gba/debug.c
        src/gba/sprites.c
        src/gba/text.c
    )
    # libtonc stays available to games that want it, but public engine headers
    # never require it.
    target_link_libraries(serval
        PUBLIC serval_portable tonc
        PRIVATE serval_warnings
    )
    # Startup code, linked first into every ROM by serval_add_rom().
    add_library(serval_crt0 OBJECT src/gba/crt0.s)

    # memcpy and friends, which GCC may call from any library (engine, libtonc,
    # game). Linked into every ROM as an object, so archive order can't leave
    # them unresolved. -fno-builtin and -fno-tree-loop-distribute-patterns stop
    # GCC from turning their loops back into calls to themselves.
    add_library(serval_libc OBJECT src/gba/libc.c)
    target_link_libraries(serval_libc PRIVATE tonc serval_warnings)
    target_compile_options(serval_libc PRIVATE -fno-builtin -fno-tree-loop-distribute-patterns)
endif()

if(SERVAL_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    enable_testing()
    add_subdirectory(tests)
endif()

if(SERVAL_BUILD_EXAMPLES AND SERVAL_TARGET_GBA AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples")
    add_subdirectory(examples)
endif()
