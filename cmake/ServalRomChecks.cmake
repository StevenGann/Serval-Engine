# serval_add_rom_checks(<target>...)
#
# Adds a test per ROM built by serval_add_rom() that runs tools/check-rom.py:
# the ROM is padded and its header is valid, with the title and game code
# serval_add_rom() was given, newlib's libc.a is not in the link map, the code
# has no BLX instruction (the ARM7TDMI has none), and a ROM linking the save
# code contains exactly one save type ID string, its save type's
# (serval_add_rom's SAVE), and a ROM without it none. Call it after the
# serval_add_rom() calls. Used by the engine's own tests and examples, and by
# tests/consumer.

function(serval_add_rom_checks)
    cmake_path(GET CMAKE_C_COMPILER PARENT_PATH toolchain_bin)
    find_program(SERVAL_OBJDUMP arm-none-eabi-objdump HINTS "${toolchain_bin}" REQUIRED)
    if(NOT SERVAL_PYTHON_EXECUTABLE)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        set(SERVAL_PYTHON_EXECUTABLE "${Python3_EXECUTABLE}")
    endif()
    set(check_rom "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/check-rom.py")
    foreach(target IN LISTS ARGN)
        if(NOT TARGET ${target})
            message(FATAL_ERROR "serval_add_rom_checks(${target}): no such target; call it after "
                                "serval_add_rom(${target} ...).")
        endif()
        get_target_property(title ${target} SERVAL_ROM_TITLE)
        get_target_property(game_code ${target} SERVAL_ROM_GAME_CODE)
        get_target_property(save_type ${target} SERVAL_ROM_SAVE_TYPE)
        if(title STREQUAL "title-NOTFOUND")
            message(FATAL_ERROR "serval_add_rom_checks(${target}): not a ROM made by "
                                "serval_add_rom().")
        endif()
        # As hexadecimal character codes, as gbafix.py gets them, so no
        # printable ASCII character needs care on the way (as text, a title
        # starting with "-" would be taken for an option of check-rom.py).
        string(HEX "${title}" title_hex)
        string(HEX "${game_code}" game_code_hex)
        set(dir "$<TARGET_FILE_DIR:${target}>")
        add_test(NAME ${target}_rom_checks
            COMMAND "${SERVAL_PYTHON_EXECUTABLE}" "${check_rom}"
                    --rom "${dir}/${target}.gba"
                    --map "${dir}/${target}.map"
                    --elf "$<TARGET_FILE:${target}>"
                    --objdump "${SERVAL_OBJDUMP}"
                    --title-hex ${title_hex}
                    --game-code-hex ${game_code_hex}
                    --save-type ${save_type})
    endforeach()
endfunction()
