# libtonc, vendored from gbadev-org/libtonc. See libtonc/VENDORED.md.
# Built with upstream's flags; the engine's warning flags are not applied.
#
# Web builds compile only its C sources: the assembly parts it needs (fast
# copies, the BIOS calls Serval uses, the sys8 font) are in src/web/.
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    file(GLOB _tonc_sources CONFIGURE_DEPENDS
        libtonc/src/*.c libtonc/src/tte/*.c libtonc/src/pre1.3/*.c)
    add_library(tonc STATIC ${_tonc_sources})
    target_include_directories(tonc SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/libtonc/include")
    target_compile_options(tonc PRIVATE -O2 -fno-strict-aliasing -w)

    # Maxmod's player and its GBA mmInit() and mmFrame(), unmodified: the GBA
    # build below less the GBA mixer (mixer.c, and mixer_asm.s, ARM
    # assembly), which the engine's web mixer replaces (src/web/mixer.c,
    # src/web/maxmod_mixer.c). Still __GBA__: the bank's GBA samples, the
    # GBA's channel layouts and timing. The attributes that put its hot
    # routines in IWRAM as ARM code (section, long_call, target("arm"),
    # which WebAssembly lacks) are defined away, as options (CMake's
    # definitions can't be function-like); nothing else in it uses those
    # words before a parenthesis.
    add_library(maxmod OBJECT
        maxmod/source/core/effect.c
        maxmod/source/core/mas.c
        maxmod/source/core/mas_arm.c
        maxmod/source/gba/main_gba.c
    )
    target_include_directories(maxmod SYSTEM
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/maxmod/include"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/maxmod/source")
    target_compile_definitions(maxmod PRIVATE __GBA__ $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)
    set_target_properties(maxmod PROPERTIES C_STANDARD 23 C_EXTENSIONS ON)
    target_compile_options(maxmod PRIVATE -O2 -w
        "-Dsection(x)=" "-Dtarget(x)=" "-Dlong_call=")
    return()
endif()

file(GLOB _tonc_sources CONFIGURE_DEPENDS
    libtonc/src/*.c
    libtonc/src/tte/*.c
    libtonc/src/tte/*.s
    libtonc/src/font/*.s
    libtonc/src/pre1.3/*.c
    libtonc/src/pre1.3/*.s
    libtonc/asm/*.s
)

add_library(tonc STATIC ${_tonc_sources})
target_include_directories(tonc SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/libtonc/include")
target_compile_options(tonc PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-O2 -fno-strict-aliasing -w>
    $<$<COMPILE_LANGUAGE:ASM>:-x assembler-with-cpp -mthumb>)

# Maxmod, vendored from BlocksDS (blocksds/maxmod). See maxmod/VENDORED.md.
# (Web builds compile it above, without its GBA mixer.) Compiled as
# upstream's own GBA build compiles it (Makefile.plat: Thumb, -O2 in debug
# and release builds alike, C23 with GNU extensions, __GBA__, NDEBUG outside
# debug builds); its hot routines put themselves in IWRAM as ARM code
# (.iwram sections, which src/gba/gba.ld places there). An object library:
# its objects go into the engine's library (CMakeLists.txt), so a game links
# Maxmod only if it registers a sound bank (audio_bank_set()), and
# --gc-sections drops what it doesn't use. The engine's warning flags are not
# applied.
add_library(maxmod OBJECT
    maxmod/source/core/effect.c
    maxmod/source/core/mas.c
    maxmod/source/core/mas_arm.c
    maxmod/source/gba/main_gba.c
    maxmod/source/gba/mixer.c
    maxmod/source/gba/mixer_asm.s
)
target_include_directories(maxmod
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/maxmod/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/maxmod/source")
target_compile_definitions(maxmod PRIVATE __GBA__ $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)
set_target_properties(maxmod PROPERTIES C_STANDARD 23 C_EXTENSIONS ON)
target_compile_options(maxmod PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-O2 -fomit-frame-pointer -w>
    $<$<COMPILE_LANGUAGE:ASM>:-x assembler-with-cpp>)
