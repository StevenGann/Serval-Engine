# libtonc, vendored from gbadev-org/libtonc. See libtonc/VENDORED.md.
# Built with upstream's flags; the engine's warning flags are not applied.
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
