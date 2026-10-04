# Shared build settings and the serval_add_rom() helper.

# Warning flags for engine code (not vendored third-party code). Link PRIVATE.
add_library(serval_warnings INTERFACE)
target_compile_options(serval_warnings INTERFACE
    $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -Wshadow -Wundef -Wstrict-prototypes -Wmissing-prototypes>)
if(SERVAL_WARNINGS_AS_ERRORS)
    target_compile_options(serval_warnings INTERFACE $<$<COMPILE_LANGUAGE:C>:-Werror>)
endif()

if(SERVAL_TARGET_GBA)
    # Put every function and object in its own section so --gc-sections can
    # drop what a game does not use.
    add_compile_options(-ffunction-sections -fdata-sections)

    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(SERVAL_LINKER_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/../src/gba/gba.ld")
    set(SERVAL_GBAFIX "${CMAKE_CURRENT_LIST_DIR}/../tools/gbafix.py")

    # libgcc (integer division and other helpers) must come last on the link
    # line, after every library that may need it.
    set(CMAKE_C_STANDARD_LIBRARIES "-lgcc")
endif()

# serval_add_rom(<target> SOURCES <files...> [TITLE <title>] [GAME_CODE <code>])
#
# Builds <target>.elf from the given sources linked against the engine, then
# converts it to <target>.gba and fixes the ROM header. The header never
# contains Nintendo's logo (see docs/licensing.md).
function(serval_add_rom target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "TITLE;GAME_CODE" "SOURCES")
    if(NOT ARG_TITLE)
        string(TOUPPER "${target}" ARG_TITLE)
    endif()
    if(NOT ARG_GAME_CODE)
        set(ARG_GAME_CODE "0000")
    endif()

    add_executable(${target} ${ARG_SOURCES} $<TARGET_OBJECTS:serval_crt0>)
    set_target_properties(${target} PROPERTIES SUFFIX ".elf")
    target_link_libraries(${target} PRIVATE serval)
    # No C library: the engine provides the few routines GCC may call
    # (src/gba/libc.c) so newlib is never linked.
    target_link_options(${target} PRIVATE
        -nostartfiles -nostdlib
        "-T${SERVAL_LINKER_SCRIPT}"
        -Wl,--gc-sections
        # IWRAM holds both code and data, so its segment is RWX by design.
        -Wl,--no-warn-rwx-segments
        "-Wl,-Map=$<TARGET_FILE_DIR:${target}>/${target}.map")
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${SERVAL_LINKER_SCRIPT}")

    set(rom "$<TARGET_FILE_DIR:${target}>/${target}.gba")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${SERVAL_OBJCOPY}" -O binary "$<TARGET_FILE:${target}>" "${rom}"
        COMMAND "${Python3_EXECUTABLE}" "${SERVAL_GBAFIX}" "${rom}"
                --title "${ARG_TITLE}" --game-code "${ARG_GAME_CODE}"
        COMMENT "Creating ${target}.gba"
        VERBATIM)
endfunction()
