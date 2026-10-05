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
