# A minimal game project that uses Serval Engine the way games do: from an
# extracted release archive, through add_subdirectory() and serval_add_rom()
# called from the game's own directory. CI builds it against the release
# archive and runs it in mGBA, so the archive is known to work for games.
#
#   cmake -S tests/consumer -B build/consumer -G Ninja \
#         -DSERVAL_ENGINE_DIR=<extracted serval-engine-X.Y.Z> \
#         -DCMAKE_TOOLCHAIN_FILE=<same>/cmake/arm-gba-toolchain.cmake
#   cmake --build build/consumer && ctest --test-dir build/consumer
cmake_minimum_required(VERSION 3.25)
project(serval_consumer LANGUAGES C)

set(SERVAL_ENGINE_DIR "" CACHE PATH "Serval Engine release directory to build against")
if(NOT EXISTS "${SERVAL_ENGINE_DIR}/serval.json")
    message(FATAL_ERROR "Set SERVAL_ENGINE_DIR to an extracted Serval Engine release")
endif()
add_subdirectory("${SERVAL_ENGINE_DIR}" serval-engine)

serval_add_rom(consumer SOURCES main.c TITLE "CONSUMER" GAME_CODE "SVCO")
# A script listing assembled at build time into consumer_script.c and .h.
serval_add_script(consumer consumer.svm)
# A game that saves, with a save type other than the default SRAM.
serval_add_rom(consumer_save SOURCES save.c TITLE "CONSUMER SV" GAME_CODE "SVCS" SAVE EEPROM8K)

enable_testing()

find_program(MGBA_ROM_TEST mgba-rom-test HINTS ENV MGBA_ROM_TEST_DIR REQUIRED)
add_test(NAME consumer_runs
    COMMAND "${MGBA_ROM_TEST}" -S 3 -R r0 -l 15 "$<TARGET_FILE_DIR:consumer>/consumer.gba")
set_tests_properties(consumer_runs PROPERTIES TIMEOUT 60)
add_test(NAME consumer_save_runs
    COMMAND "${MGBA_ROM_TEST}" -S 3 -R r0 -l 15
            "$<TARGET_FILE_DIR:consumer_save>/consumer_save.gba")
set_tests_properties(consumer_save_runs PROPERTIES TIMEOUT 60)

# The checker comes from this repository: release archives don't ship it.
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/ServalRomChecks.cmake")
serval_add_rom_checks(consumer consumer_save)

# Game code must get its own sections, or --gc-sections can't drop unused
# game functions.
add_test(NAME consumer_unused_code_is_dropped
    COMMAND "${CMAKE_COMMAND}" -DMAP=$<TARGET_FILE_DIR:consumer>/consumer.map
            -P "${CMAKE_CURRENT_LIST_DIR}/check-gc-sections.cmake")
