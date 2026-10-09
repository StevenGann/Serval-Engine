# serval_add_header_check(<target> [<definitions>...])
#
# An object library with one translation unit per public header, each
# including only that header, compiled with the given definitions and nothing
# but include/ on the include path.
function(serval_add_header_check target)
    file(GLOB headers CONFIGURE_DEPENDS RELATIVE "${PROJECT_SOURCE_DIR}/include"
         "${PROJECT_SOURCE_DIR}/include/serval/*.h")
    set(sources "")
    foreach(header IN LISTS headers)
        string(MAKE_C_IDENTIFIER "${header}" name)
        set(source "${CMAKE_CURRENT_BINARY_DIR}/${target}/${name}.c")
        file(CONFIGURE OUTPUT "${source}" CONTENT
             "// Generated: ${header} must compile on its own.\n#include \"${header}\"\n\nvoid ${name}_check(void);\nvoid ${name}_check(void) {}\n")
        list(APPEND sources "${source}")
    endforeach()
    add_library(${target} OBJECT ${sources})
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_compile_definitions(${target} PRIVATE ${ARGN})
    target_link_libraries(${target} PRIVATE serval_warnings)
endfunction()

# Planned API (SERVAL_PLANNED, docs/development.md#planned-api): every planned
# name in the public headers is used in planned/, and every use warns, with
# this build's compiler and target flags (tools/check-planned.py). A test on
# the host and the GBA; part of the build (ALL) in web builds, which run no
# tests.
separate_arguments(_serval_planned_flags NATIVE_COMMAND "${CMAKE_C_FLAGS}")
if(SERVAL_TARGET_GBA)
    list(APPEND _serval_planned_flags -DSERVAL_GBA)
elseif(SERVAL_TARGET_WEB)
    list(APPEND _serval_planned_flags -DSERVAL_WEB)
endif()
set(_serval_planned_check
    "${SERVAL_PYTHON_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check-planned.py"
    --include "${PROJECT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/planned"
    -- "${CMAKE_C_COMPILER}" ${_serval_planned_flags})
if(SERVAL_TARGET_WEB)
    file(GLOB _serval_planned_inputs CONFIGURE_DEPENDS
         "${PROJECT_SOURCE_DIR}/include/serval/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/planned/*.c")
    set(_serval_planned_stamp "${CMAKE_CURRENT_BINARY_DIR}/planned_api.stamp")
    add_custom_command(OUTPUT "${_serval_planned_stamp}"
        COMMAND ${_serval_planned_check}
        COMMAND "${CMAKE_COMMAND}" -E touch "${_serval_planned_stamp}"
        DEPENDS "${PROJECT_SOURCE_DIR}/tools/check-planned.py"
                "${PROJECT_SOURCE_DIR}/tools/svlua.py" ${_serval_planned_inputs}
        COMMENT "Checking that every planned API warns (tools/check-planned.py)"
        VERBATIM)
    add_custom_target(serval_planned_api_check ALL DEPENDS "${_serval_planned_stamp}")
    return()
endif()
add_test(NAME planned_api COMMAND ${_serval_planned_check})

add_library(serval_test_harness STATIC test.c ecs_tests.c random_tests.c text_format_tests.c math_tests.c
    physics_tests.c map_tests.c anim_tests.c psg_sequencer_tests.c save_tests.c input_tests.c path_tests.c splash_logic_tests.c
    vm_tests.c color_tests.c planned_audio_tests.c planned_sprites_tests.c planned_map_tests.c
    planned_screen_tests.c)
target_link_libraries(serval_test_harness
    PUBLIC serval_portable
    PRIVATE serval_warnings)

if(SERVAL_TARGET_GBA)
    serval_add_rom(serval_tests
        SOURCES rom/main.c rom/core_tests.c rom/libc_tests.c rom/sprite_tests.c rom/gba_text_tests.c rom/audio_tests.c rom/splash_tests.c rom/map_tests.c rom/present_tests.c rom/save_tests.c rom/ecs_cost_tests.c
                rom/vm_platform_tests.c
                rom/sprite_tiles_tests.c
                rom/blend_tests.c
                rom/compat_tonc_first.c rom/compat_serval_first.c
        TITLE "SERVAL TESTS"
        GAME_CODE "SVTS")
    target_link_libraries(serval_tests PRIVATE serval_test_harness serval_warnings)

    # One more test ROM per save type other than SRAM: the save suites, on
    # the ROM's Flash or EEPROM (tests/rom/save_main.c).
    set(save_test_types FLASH64K FLASH128K EEPROM8K EEPROM512)
    set(save_test_codes SVF6 SVF1 SVE8 SVE5)
    foreach(type code IN ZIP_LISTS save_test_types save_test_codes)
        string(TOLOWER "${type}" name)
        serval_add_rom(serval_tests_${name}
            SOURCES rom/save_main.c rom/save_tests.c
            TITLE "SV ${type}"
            GAME_CODE "${code}"
            SAVE ${type})
        target_link_libraries(serval_tests_${name} PRIVATE serval_test_harness serval_warnings)
        serval_add_rom_checks(serval_tests_${name})
    endforeach()

    # mgba-rom-test runs a ROM headless until SWI 3 and exits with r0: 0 if
    # every check passed, 1 otherwise. Build it with tools/build-mgba-rom-test.sh.
    # rom/run-rom-test.cmake also checks mGBA's log: that it detected the save
    # type the ROM was built for, and reported no malformed Flash or EEPROM
    # access.
    find_program(MGBA_ROM_TEST mgba-rom-test HINTS ENV MGBA_ROM_TEST_DIR)
    set(save_access_errors "Bad flash|Unsupported flash|Flash entered bad state|beyond end of EEPROM|Bad flash bank")
    if(MGBA_ROM_TEST)
        add_test(NAME rom_tests
            COMMAND "${CMAKE_COMMAND}" "-DMGBA_ROM_TEST=${MGBA_ROM_TEST}"
                    "-DROM=$<TARGET_FILE_DIR:serval_tests>/serval_tests.gba"
                    "-DEXPECT=Detected SRAM savegame"
                    "-DREJECT=Detected (Flash|EEPROM)"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/rom/run-rom-test.cmake")
        set_tests_properties(rom_tests PROPERTIES TIMEOUT 60)
        set(expect_FLASH64K "Detected Flash savegame")
        set(reject_FLASH64K "Detected (SRAM|EEPROM)|Updating flash chip")
        set(expect_FLASH128K "Detected Flash savegame@@Updating flash chip from 512kb to 1Mb")
        set(reject_FLASH128K "Detected (SRAM|EEPROM)")
        # Once a bank switch has made it a 128 KiB chip, mGBA reports Sanyo's
        # ID, and logs (and ignores) the second "leave ID mode" write that
        # real Sanyo chips need (src/gba/save_flash.c).
        set(allow_FLASH128K "Bad flash write: 0x5555 = 0xf0")
        set(expect_EEPROM8K "Detected EEPROM savegame")
        set(reject_EEPROM8K "Detected (SRAM|Flash)")
        set(expect_EEPROM512 "Detected EEPROM savegame")
        set(reject_EEPROM512 "Detected (SRAM|Flash)")
        foreach(type IN LISTS save_test_types)
            string(TOLOWER "${type}" name)
            add_test(NAME rom_tests_${name}
                COMMAND "${CMAKE_COMMAND}" "-DMGBA_ROM_TEST=${MGBA_ROM_TEST}"
                        "-DROM=$<TARGET_FILE_DIR:serval_tests_${name}>/serval_tests_${name}.gba"
                        "-DEXPECT=${expect_${type}}"
                        "-DREJECT=${reject_${type}}|${save_access_errors}"
                        "-DALLOW=${allow_${type}}"
                        -P "${CMAKE_CURRENT_SOURCE_DIR}/rom/run-rom-test.cmake")
            set_tests_properties(rom_tests_${name} PROPERTIES TIMEOUT 120)
        endforeach()
    elseif(SERVAL_REQUIRE_ROM_TESTS)
        message(FATAL_ERROR "mgba-rom-test not found, and SERVAL_REQUIRE_ROM_TESTS is ON. "
                            "Build it with tools/build-mgba-rom-test.sh and set MGBA_ROM_TEST_DIR.")
    else()
        message(STATUS "mgba-rom-test not found: ROM tests are built but not run")
    endif()

    # Padding and header (ROMs of 256 KiB or less can be mistaken for multiboot
    # images: white screen in mGBA 0.8), no libc.a in the map, no BLX.
    serval_add_rom_checks(serval_tests)

    # serval_add_rom()'s TITLE and GAME_CODE with every printable ASCII
    # character, spaces at either end, and text that means something to
    # CMake or a shell ("$<1:X>", "${A}", "@A@", ";", "[;]", "\;", a leading
    # "-"): each must reach the ROM header unchanged, through
    # cmake_parse_arguments() and the build command that runs gbafix.py. The
    # <rom>_fields tests compare the header with the character codes of the
    # text given here, not with what serval_add_rom() made of it. Then a ROM
    # without TITLE and GAME_CODE: the target name in upper case, cut to 12
    # characters, and 0000; and a title that CMake takes for false.
    set(printable "")
    set(punctuation "")
    foreach(code RANGE 32 126)
        string(ASCII ${code} c)
        string(APPEND printable "${c}")
        if(NOT c MATCHES "[A-Za-z0-9]")
            string(APPEND punctuation "${c}")
        endif()
    endforeach()
    foreach(i RANGE 7)
        math(EXPR at "${i} * 12")
        string(SUBSTRING "${printable}" ${at} 12 title_${i})
        math(EXPR at "${i} * 4")
        string(SUBSTRING "${punctuation}" ${at} 4 code_${i})
    endforeach()
    string(APPEND title_7 " ") # the last 11, and a space
    set(title_8 "$<1:X>\${A}@;")
    set(code_8 "~AB ")
    set(title_9 "-[;]\\;@A@")
    set(code_9 "$<A>")
    set(title_10 "0")
    set(code_10 "OFF ")
    set(header_roms "")
    foreach(i RANGE 10)
        set(rom serval_header_${i})
        serval_add_rom(${rom} SOURCES rom/header_main.c
            TITLE "${title_${i}}" GAME_CODE "${code_${i}}")
        target_link_libraries(${rom} PRIVATE serval_warnings)
        string(HEX "${title_${i}}" title_hex)
        string(HEX "${code_${i}}" code_hex)
        add_test(NAME ${rom}_fields
            COMMAND "${SERVAL_PYTHON_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check-rom.py"
                    --rom "$<TARGET_FILE_DIR:${rom}>/${rom}.gba"
                    --title-hex ${title_hex} --game-code-hex ${code_hex})
        list(APPEND header_roms ${rom})
    endforeach()
    serval_add_rom(serval_header_default SOURCES rom/header_main.c)
    target_link_libraries(serval_header_default PRIVATE serval_warnings)
    add_test(NAME serval_header_default_fields
        COMMAND "${SERVAL_PYTHON_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check-rom.py"
                --rom "$<TARGET_FILE_DIR:serval_header_default>/serval_header_default.gba"
                --title SERVAL_HEADE --game-code 0000)
    serval_add_rom_checks(${header_roms} serval_header_default)

    # Every public header compiles on its own for the GBA, without libtonc on
    # the include path.
    serval_add_header_check(serval_gba_header_check SERVAL_GBA)
else()
    # The web backend's renderer and sound are plain C, tested natively on
    # arrays standing in for GBA memory.
    add_executable(serval_host_tests host/main.c web_ppu_tests.c web_apu_tests.c
                   ../src/web/ppu.c ../src/web/apu.c)
    target_link_libraries(serval_host_tests PRIVATE serval_test_harness serval_warnings)
    add_test(NAME host_tests COMMAND serval_host_tests)

    # serval_add_rom()'s checks of TITLE, GAME_CODE and SAVE, at configure time
    # (scratch projects configured with bad and good values).
    add_test(NAME rom_fields_configure
        COMMAND "${CMAKE_COMMAND}" "-DSERVAL_DIR=${PROJECT_SOURCE_DIR}"
                "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/rom_fields"
                "-DGENERATOR=${CMAKE_GENERATOR}" "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/rom_fields_test.cmake")
    # The ROM header fixer (tools/gbafix.py): the header it writes, and every
    # value it refuses.
    add_test(NAME gbafix_tool
        COMMAND "${SERVAL_PYTHON_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/gbafix_test.py")
    # The script assembler and disassembler (tools/svm.py) has its own tests:
    # the golden bytes, every operand layout, the round trip, the errors.
    add_test(NAME svm_tool
        COMMAND "${SERVAL_PYTHON_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/svm_test.py")
    # Runs a script blob on the VM and prints what it did (tests/svlua/runner.c),
    # for the compiler's tests: compiled Lua run on the real VM.
    add_executable(svlua_runner svlua/runner.c)
    target_link_libraries(svlua_runner PRIVATE serval_portable serval_warnings)
    # The Lua-subset compiler (tools/svlua.py, docs/lua.md): the lexer and
    # parser, every rejected construct, types, golden listings, compiled
    # programs run on the VM by svlua_runner, fireflies.lua.
    add_test(NAME svlua_tool
        COMMAND "${CMAKE_COMMAND}" -E env "SERVAL_SVLUA_RUNNER=$<TARGET_FILE:svlua_runner>"
                "${SERVAL_PYTHON_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/svlua_test.py")
    # docs/lua.md "Testing": the programs in svlua/diff/ run under real Lua
    # (Lua 5.4 built with LUA_32BITS, named by SERVAL_LUA32 when the test
    # runs; tools/setup-dev.sh --with-lua32) and compiled on the VM, frame by
    # frame, and must agree. Skipped without SERVAL_LUA32.
    add_test(NAME svlua_difftest
        COMMAND "${CMAKE_COMMAND}" -E env "SERVAL_SVLUA_RUNNER=$<TARGET_FILE:svlua_runner>"
                "${SERVAL_PYTHON_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/svlua_difftest.py")
    set_tests_properties(svlua_difftest PROPERTIES SKIP_RETURN_CODE 77)
    # The logo PNGs in docs/images are the splash's own drawing, made by
    # tools/logo-png.py; this fails when splash_art.c changes until they are
    # regenerated.
    add_test(NAME logo_png
        COMMAND "${SERVAL_PYTHON_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/logo-png.py"
                --check "${PROJECT_SOURCE_DIR}/docs/images" --cc "${CMAKE_C_COMPILER}")

    # Games must never need a third-party header. libtonc is not on the host
    # include path, so these compile only if the public headers and the
    # examples use nothing but Serval's API.
    file(GLOB _serval_example_sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/examples/*/*.c")
    add_library(serval_api_only_check OBJECT public_headers.c ${_serval_example_sources}
        consumer/main.c consumer/save.c)
    target_include_directories(serval_api_only_check PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(serval_api_only_check PRIVATE serval_warnings)
    add_dependencies(serval_host_tests serval_api_only_check)
    # The script listings those games include the generated headers of are
    # assembled for this check too, which also runs the assembler on them.
    serval_add_script(serval_api_only_check consumer/consumer.lua)
    serval_add_script(serval_api_only_check ../examples/fireflies/fireflies.lua
                      HEADERS ../examples/fireflies/game.h serval/ecs.h serval/core.h
                              serval/sprites.h serval/path.h serval/screen.h)
    # A hand-written listing (.svm) through serval_add_script() too.
    add_subdirectory(svm)
    add_dependencies(serval_host_tests serval_svm_listing_check)

    # Every public header compiles on its own (includes what it uses).
    serval_add_header_check(serval_header_check)
    add_dependencies(serval_host_tests serval_header_check)
endif()
