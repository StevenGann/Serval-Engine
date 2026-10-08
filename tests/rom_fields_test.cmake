# serval_add_rom()'s checks of TITLE, GAME_CODE and SAVE at configure time
# (cmake/Serval.cmake), run by CTest in the host build (rom_fields_configure).
#
# Each case configures a scratch project that includes cmake/Serval.cmake and
# calls serval_add_rom(), and checks the message configuration stops with: the
# error naming the target, the field and what is wrong, or, for valid fields,
# the next error, about the engine (the scratch project doesn't configure
# one). Every case runs twice when CMake has policy CMP0174 (3.31 and later):
# unset, as the engine's CMakeLists.txt leaves it, and NEW, which changes how
# cmake_parse_arguments() reports an empty value. What valid fields become in
# the ROM header is checked by the header ROMs in tests/CMakeLists.txt (GBA).
#
#   cmake -DSERVAL_DIR=<engine> -DWORK=<scratch directory>
#         [-DGENERATOR=<generator> -DMAKE_PROGRAM=<path>] -P rom_fields_test.cmake

foreach(var IN ITEMS SERVAL_DIR WORK)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "rom_fields_test.cmake: set ${var}")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK}")
set(failures 0)
set(cases 0)
set(engine_missing "the engine is not configured (serval_save_sram is missing).")
string(ASCII 9 tab)
string(ASCII 127 del)

# expect(<message> <arguments>)
#
# Configures serval_add_rom(<arguments>) (CMake source, as a game writes it)
# and checks that it fails with "serval_add_rom(<target>): <message>", the
# target being the first argument. Whitespace runs count as one space, since
# CMake wraps long messages.
function(expect message arguments)
    string(REGEX MATCH "^[^ ]+" target "${arguments}")
    set(expected "serval_add_rom(${target}): ${message}")
    string(REGEX REPLACE "[ \t\n]+" " " expected "${expected}")
    set(policies unset)
    if(POLICY CMP0174)
        list(APPEND policies NEW)
    endif()
    foreach(policy IN LISTS policies)
        math(EXPR n "${cases} + 1")
        set(cases ${n} PARENT_SCOPE)
        set(cases ${n})
        set(policy_line "")
        if(policy STREQUAL "NEW")
            set(policy_line "cmake_policy(SET CMP0174 NEW)")
        endif()
        file(WRITE "${WORK}/CMakeLists.txt"
             "cmake_minimum_required(VERSION 3.25)\n"
             "project(rom_fields LANGUAGES NONE)\n"
             "${policy_line}\n"
             "include(\"${SERVAL_DIR}/cmake/Serval.cmake\")\n"
             "serval_add_rom(${arguments})\n")
        set(generator_args "")
        if(GENERATOR)
            list(APPEND generator_args -G "${GENERATOR}")
        endif()
        if(MAKE_PROGRAM)
            list(APPEND generator_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
        endif()
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -S "${WORK}" -B "${WORK}/build" ${generator_args} -Wno-dev
            RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
        string(REGEX REPLACE "[ \t\n]+" " " flat "${output}")
        string(FIND "${flat}" "${expected}" at)
        if(result EQUAL 0 OR at EQUAL -1)
            math(EXPR f "${failures} + 1")
            set(failures ${f} PARENT_SCOPE)
            set(failures ${f})
            message("FAIL: serval_add_rom(${arguments}) (policy CMP0174 ${policy})\n"
                    "  expected: ${expected}\n  got:\n${output}")
        endif()
    endforeach()
endfunction()

# TITLE: 1 to 12 printable ASCII characters.
expect("TITLE is empty; give 1 to 12 printable ASCII characters, or leave TITLE out to use the target name."
       [[game SOURCES main.c TITLE ""]])
expect("TITLE is empty; give 1 to 12 printable ASCII characters, or leave TITLE out to use the target name."
       [[game SOURCES main.c TITLE GAME_CODE "ABCD"]])
expect("TITLE is empty; give 1 to 12 printable ASCII characters, or leave TITLE out to use the target name."
       [[game SOURCES main.c GAME_CODE "ABCD" TITLE]])
expect([[TITLE can have at most 12 characters; "THIRTEEN CHRS" has 13.]]
       [[game SOURCES main.c TITLE "THIRTEEN CHRS"]])
expect([[TITLE can have at most 12 characters; "  TWELVE+TWO " has 13.]]
       [[game SOURCES main.c TITLE "  TWELVE+TWO "]])
expect("TITLE can't contain a control character: use ASCII letters, digits, spaces and punctuation."
       "game SOURCES main.c TITLE \"A${tab}B\"")
expect("TITLE can't contain a control character: use ASCII letters, digits, spaces and punctuation."
       "game SOURCES main.c TITLE \"A${del}B\"")
expect([[TITLE can't contain "É": use ASCII letters, digits, spaces and punctuation.]]
       [[game SOURCES main.c TITLE "CAFÉ"]])
expect([[TITLE can't contain "日": use ASCII letters, digits, spaces and punctuation.]]
       [[game SOURCES main.c TITLE "日本"]])

# GAME_CODE: exactly 4 printable ASCII characters, never padded.
expect([[GAME_CODE must have exactly 4 characters; "" has 0.]]
       [[game SOURCES main.c GAME_CODE ""]])
expect([[GAME_CODE must have exactly 4 characters; "" has 0.]]
       [[game SOURCES main.c GAME_CODE SAVE SRAM]])
expect([[GAME_CODE must have exactly 4 characters; "0" has 1.]]
       [[game SOURCES main.c GAME_CODE "0"]])
expect([[GAME_CODE must have exactly 4 characters; "ABC" has 3.]]
       [[game SOURCES main.c GAME_CODE "ABC"]])
expect([[GAME_CODE must have exactly 4 characters; "ABCDE" has 5.]]
       [[game SOURCES main.c GAME_CODE "ABCDE"]])
expect([[GAME_CODE must have exactly 4 characters; "A;B" has 3.]]
       [[game SOURCES main.c GAME_CODE "A;B"]])
expect("GAME_CODE can't contain a control character: use ASCII letters, digits, spaces and punctuation."
       "game SOURCES main.c GAME_CODE \"AB${tab}C\"")
expect([[GAME_CODE can't contain "ß": use ASCII letters, digits, spaces and punctuation.]]
       [[game SOURCES main.c GAME_CODE "AßC"]])

# SAVE: one of the five types.
expect([[SAVE "" is not a save type; use one of SRAM, FLASH64K, FLASH128K, EEPROM8K, EEPROM512.]]
       [[game SOURCES main.c SAVE ""]])
expect([[SAVE "sram" is not a save type; use one of SRAM, FLASH64K, FLASH128K, EEPROM8K, EEPROM512.]]
       [[game SOURCES main.c SAVE sram]])

# Valid: past the checks, to the missing engine.
expect("${engine_missing}" [[game SOURCES main.c]])
expect("${engine_missing}" [[a_target_name_longer_than_twelve SOURCES main.c]])
expect("${engine_missing}" [[game SOURCES main.c TITLE "A" GAME_CODE "    "]])
expect("${engine_missing}" [[game SOURCES main.c TITLE "  SPACES  " GAME_CODE " AB "]])
expect("${engine_missing}" [[game SOURCES main.c TITLE "TWELVE CHARS"]])
# CMake's false constants are titles like any other.
foreach(title IN ITEMS 0 N NO OFF FALSE IGNORE NOTFOUND X-NOTFOUND)
    expect("${engine_missing}" "game SOURCES main.c TITLE \"${title}\"")
endforeach()
# Every printable ASCII character, in titles of 12 and game codes of 4
# (written as CMake source: \, " and $ escaped).
set(printable "")
foreach(code RANGE 32 126)
    string(ASCII ${code} c)
    if(c MATCHES "[\\\"$]")
        set(c "\\${c}")
    endif()
    string(APPEND printable "${c}")
endforeach()
string(LENGTH "${printable}" length)
set(start 0)
while(start LESS length)
    # 12 characters, an escape counting with the character it escapes.
    set(title "")
    set(count 0)
    while(count LESS 12 AND start LESS length)
        string(SUBSTRING "${printable}" ${start} 1 c)
        if(c STREQUAL "\\")
            string(SUBSTRING "${printable}" ${start} 2 c)
        endif()
        string(LENGTH "${c}" n)
        math(EXPR start "${start} + ${n}")
        string(APPEND title "${c}")
        math(EXPR count "${count} + 1")
    endwhile()
    expect("${engine_missing}" "game SOURCES main.c TITLE \"${title}\"")
endwhile()
set(start 0)
while(start LESS length)
    set(code "")
    set(count 0)
    while(count LESS 4)
        if(start LESS length)
            string(SUBSTRING "${printable}" ${start} 1 c)
            if(c STREQUAL "\\")
                string(SUBSTRING "${printable}" ${start} 2 c)
            endif()
            string(LENGTH "${c}" n)
            math(EXPR start "${start} + ${n}")
        else()
            set(c "~")
        endif()
        string(APPEND code "${c}")
        math(EXPR count "${count} + 1")
    endwhile()
    expect("${engine_missing}" "game SOURCES main.c GAME_CODE \"${code}\"")
endwhile()

file(REMOVE_RECURSE "${WORK}")
if(cases LESS 64)
    message(FATAL_ERROR "only ${cases} serval_add_rom() configurations ran")
endif()
if(failures GREATER 0)
    message(FATAL_ERROR "${failures} of ${cases} serval_add_rom() configurations failed")
endif()
message("all ${cases} serval_add_rom() configurations stopped as expected")
