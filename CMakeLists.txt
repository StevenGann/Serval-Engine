cmake_minimum_required(VERSION 3.25)

# serval.json is the single source of truth for the engine version.
# SERVAL_VERSION_STRING is the full version (e.g. 0.2.0-rc.1); project() only
# accepts numeric components, so it gets the version without the pre-release
# or build suffix.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/serval.json" _serval_manifest)
string(JSON SERVAL_VERSION_STRING GET "${_serval_manifest}" version)
if(NOT SERVAL_VERSION_STRING MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)([-+].*)?$")
    message(FATAL_ERROR "serval.json: version '${SERVAL_VERSION_STRING}' is not X.Y.Z[-pre][+build]")
endif()
set(SERVAL_VERSION "${CMAKE_MATCH_1}")
string(JSON _serval_min_gcc ERROR_VARIABLE _serval_no_min_gcc
    GET "${_serval_manifest}" toolchain gcc)

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
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(SERVAL_TARGET_WEB ON)
else()
    set(SERVAL_TARGET_WEB OFF)
endif()

# serval.json's toolchain.gcc (e.g. ">=15.3") is the GCC the engine is tested
# with. Older compilers may work, but are not supported.
if(SERVAL_TARGET_GBA AND NOT _serval_no_min_gcc
   AND _serval_min_gcc MATCHES "^>=([0-9]+(\\.[0-9]+)*)$")
    if(CMAKE_C_COMPILER_VERSION VERSION_LESS CMAKE_MATCH_1)
        message(WARNING "Serval Engine ${SERVAL_VERSION_STRING} needs arm-none-eabi-gcc "
                        "${_serval_min_gcc} (serval.json), but ${CMAKE_C_COMPILER} is "
                        "${CMAKE_C_COMPILER_VERSION}. Install a newer ARM GNU Toolchain.")
    endif()
endif()

option(SERVAL_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(SERVAL_BUILD_TESTS "Build the test suites" ${PROJECT_IS_TOP_LEVEL})
option(SERVAL_BUILD_EXAMPLES "Build the example ROMs" ${PROJECT_IS_TOP_LEVEL})
option(SERVAL_REQUIRE_ROM_TESTS
    "Fail configuration if mgba-rom-test is missing, instead of skipping the ROM tests" OFF)

include(cmake/Serval.cmake)

# Platform-neutral engine code: builds for the GBA and for the host, so its
# logic can be unit tested natively. See docs/platforms.md.
set(SERVAL_PORTABLE_SOURCES
    src/core/map.c
    src/core/psg_sequencer.c
    src/core/random.c
    src/core/sprite_table.c
    src/core/text_format.c
    src/core/trig.c
    src/ecs/animate.c
    src/ecs/ecs.c
    src/ecs/map_movement.c
    src/ecs/physics.c
)

if(SERVAL_TARGET_GBA)
    enable_language(ASM)
    add_subdirectory(third_party)

    # The whole engine is one library on the GBA. Portable and GBA code call
    # each other (e.g. the ECS reports warnings through debug.c), and the
    # linker only resolves references between libraries in link order, but
    # within one library in any order.
    add_library(serval STATIC
        ${SERVAL_PORTABLE_SOURCES}
        src/gba/core.c
        src/gba/debug.c
        src/gba/map.c
        src/gba/music.c
        src/gba/psg.c
        src/gba/screen.c
        src/gba/splash.c
        src/gba/sprites.c
        src/gba/text.c
    )
    target_compile_definitions(serval PUBLIC SERVAL_GBA)
    # libtonc stays available to games that want it, but public engine headers
    # never require it.
    target_link_libraries(serval PUBLIC tonc PRIVATE serval_warnings)
    # Game code (serval_add_rom() targets, and libraries linking serval) gets
    # its own sections too, so --gc-sections drops what the game doesn't use.
    target_compile_options(serval INTERFACE -ffunction-sections -fdata-sections)
    add_library(serval_portable ALIAS serval)
    set(serval_engine_target serval)
elseif(SERVAL_TARGET_WEB)
    # Web builds compile the GBA backend unchanged to WebAssembly and run it on
    # virtual GBA hardware: src/web/ stands in for the BIOS and libtonc's
    # assembly, and draws and plays what the hardware would. See
    # docs/platforms.md.
    add_subdirectory(third_party)
    _serval_web_font("${CMAKE_CURRENT_BINARY_DIR}/sys8_glyphs.c")
    add_library(serval STATIC
        ${SERVAL_PORTABLE_SOURCES}
        src/gba/core.c
        src/gba/map.c
        src/gba/music.c
        src/gba/psg.c
        src/gba/screen.c
        src/gba/splash.c
        src/gba/sprites.c
        src/gba/text.c
        src/web/apu.c
        src/web/platform.c
        src/web/ppu.c
        "${CMAKE_CURRENT_BINARY_DIR}/sys8_glyphs.c"
    )
    target_compile_definitions(serval PUBLIC SERVAL_WEB)
    target_link_libraries(serval PUBLIC tonc PRIVATE serval_warnings)
    add_library(serval_portable ALIAS serval)
    set(serval_engine_target serval)
else()
    # Host builds (unit tests) get only the portable code, plus host versions
    # of the platform functions it needs.
    add_library(serval_portable STATIC ${SERVAL_PORTABLE_SOURCES} src/host/platform.c)
    target_link_libraries(serval_portable PRIVATE serval_warnings)
    set(serval_engine_target serval_portable)
endif()

target_include_directories(${serval_engine_target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# Debug builds report API misuse as warnings (src/core/warn.h). Public, so
# games can key their own debug code off SERVAL_DEBUG too.
target_compile_definitions(${serval_engine_target}
    PUBLIC $<$<CONFIG:Debug,RelWithDebInfo>:SERVAL_DEBUG>)

if(SERVAL_TARGET_GBA)
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

# Web builds have no test suites of their own: the host tests cover src/web/'s
# renderer and sound, and the GBA tests the rest.
if(SERVAL_BUILD_TESTS AND NOT SERVAL_TARGET_WEB AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    enable_testing()
    if(SERVAL_TARGET_GBA)
        include(cmake/ServalRomChecks.cmake)
    endif()
    add_subdirectory(tests)
endif()

if(SERVAL_BUILD_EXAMPLES AND (SERVAL_TARGET_GBA OR SERVAL_TARGET_WEB) AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples")
    add_subdirectory(examples)
endif()
