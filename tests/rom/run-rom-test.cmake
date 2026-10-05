# Runs a test ROM in mgba-rom-test and checks its result and mGBA's log:
#
#   cmake -DMGBA_ROM_TEST=<path> -DROM=<rom.gba> [-DEXPECT=<regex>[@@<regex>...]]
#         [-DREJECT=<regex>] [-DALLOW=<regex>] -P run-rom-test.cmake
#
# Fails if the ROM's checks failed (exit code), if text matching an EXPECT
# regex is missing from the log (e.g. mGBA's "Detected Flash savegame": it
# gave the ROM the save memory it expects), or if text matching REJECT is
# there (e.g. mGBA reporting a malformed Flash command), not counting text
# matching ALLOW (a known, harmless report). EXPECT's regexes are
# separated by "@@" (add_test would split a CMake list).
string(REPLACE "@@" ";" EXPECT "${EXPECT}")
execute_process(
    COMMAND "${MGBA_ROM_TEST}" -S 3 -R r0 -l 79 "${ROM}"
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
    RESULT_VARIABLE result)
message("${output}")
set(problems "")
if(NOT result EQUAL 0)
    list(APPEND problems "mgba-rom-test exited with ${result}")
endif()
foreach(regex IN LISTS EXPECT)
    if(NOT output MATCHES "${regex}")
        list(APPEND problems "the log has no line matching '${regex}'")
    endif()
endforeach()
set(checked "${output}")
if(ALLOW)
    string(REGEX REPLACE "${ALLOW}" "" checked "${checked}")
endif()
if(REJECT AND checked MATCHES "${REJECT}")
    list(APPEND problems "the log has '${CMAKE_MATCH_0}' (matching '${REJECT}')")
endif()
if(problems)
    list(JOIN problems "\n  " problems)
    message(FATAL_ERROR "${ROM}:\n  ${problems}")
endif()
