# serval_add_rom_checks(<target>...)
#
# Adds a test per ROM built by serval_add_rom() that runs tools/check-rom.py:
# the ROM is padded and its header is valid, newlib's libc.a is not in the
# link map, the code has no BLX instruction (the ARM7TDMI has none), and a ROM
# linking the save code contains exactly one save type ID string, its save
# type's (serval_add_rom's SAVE), and a ROM without it none.
# Used by the engine's own tests and examples, and by tests/consumer.

function(serval_add_rom_checks)
    cmake_path(GET CMAKE_C_COMPILER PARENT_PATH toolchain_bin)
    find_program(SERVAL_OBJDUMP arm-none-eabi-objdump HINTS "${toolchain_bin}" REQUIRED)
    if(NOT SERVAL_PYTHON_EXECUTABLE)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        set(SERVAL_PYTHON_EXECUTABLE "${Python3_EXECUTABLE}")
    endif()
    set(check_rom "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/check-rom.py")
    foreach(target IN LISTS ARGN)
        set(dir "$<TARGET_FILE_DIR:${target}>")
        add_test(NAME ${target}_rom_checks
            COMMAND "${SERVAL_PYTHON_EXECUTABLE}" "${check_rom}"
                    --rom "${dir}/${target}.gba"
                    --map "${dir}/${target}.map"
                    --elf "$<TARGET_FILE:${target}>"
                    --objdump "${SERVAL_OBJDUMP}"
                    --title "$<TARGET_PROPERTY:${target},SERVAL_ROM_TITLE>"
                    --game-code "$<TARGET_PROPERTY:${target},SERVAL_ROM_GAME_CODE>"
                    --save-type "$<TARGET_PROPERTY:${target},SERVAL_ROM_SAVE_TYPE>")
    endforeach()
endfunction()
