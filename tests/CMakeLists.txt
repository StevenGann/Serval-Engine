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
# this build's compiler and target flags (tools/check-planned.py); so do the
# tests' own (planned/fake.h), which keep the check checking when the headers
# declare nothing planned. A test on
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
         "${PROJECT_SOURCE_DIR}/include/serval/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/planned/*.c"
         "${CMAKE_CURRENT_SOURCE_DIR}/planned/*.h")
    set(_serval_planned_stamp "${CMAKE_CURRENT_BINARY_DIR}/planned_api.stamp")
    add_custom_command(OUTPUT "${_serval_planned_stamp}"
        COMMAND ${_serval_planned_check}
        COMMAND "${CMAKE_COMMAND}" -E touch "${_serval_planned_stamp}"
        DEPENDS "${PROJECT_SOURCE_DIR}/tools/check-planned.py"
                "${PROJECT_SOURCE_DIR}/tools/svlua.py" ${_serval_planned_inputs}
        COMMENT "Checking that every planned API warns (tools/check-planned.py)"
        VERBATIM)
    add_custom_target(serval_planned_api_check ALL DEPENDS "${_serval_planned_stamp}")
    # lz77: the suites that need the web's BIOS stand-ins, in a page of their
    # own (web/main.c), which CI runs with tools/web-shots.py: the LZ77
    # loaders' ROM suite and their check.
    serval_add_rom(serval_web_tests SOURCES web/main.c test.c lz77_tests.c rom/lz77_tests.c
                   TITLE "SERVAL TESTS" GAME_CODE "SVTW")
    target_link_libraries(serval_web_tests PRIVATE serval_warnings)
    serval_add_lz77(serval_web_tests lz77_tiles lz77/padded.inc
                    ../examples/platformer/castle_tiles.inc ../examples/platformer/dragon_tiles.inc)
    # debug link: the debug link end to end (tools/svlink_test.py, run by
    # tools/svlink.py in headless Chrome): fireflies' debug page against its
    # blob and pc map, and the same page built for release (a build of its
    # own, svlink-release.cmake, set up first), which has no agent. Exits 77,
    # skipped, without Chrome. Only in builds with the agent.
    if(SERVAL_BUILD_EXAMPLES AND SERVAL_LINK AND CMAKE_BUILD_TYPE MATCHES "^(Debug|RelWithDebInfo)$")
        set(svlink_release "${CMAKE_CURRENT_BINARY_DIR}/svlink-release")
        add_test(NAME svlink_release_page
            COMMAND "${CMAKE_COMMAND}" "-DSOURCE=${PROJECT_SOURCE_DIR}" "-DBUILD=${svlink_release}"
                    "-DGENERATOR=${CMAKE_GENERATOR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/web/svlink-release.cmake")
        set_tests_properties(svlink_release_page PROPERTIES FIXTURES_SETUP svlink_release TIMEOUT 900)
        set(examples "${PROJECT_BINARY_DIR}/examples")
        # debug link (E9): hot reload end to end. fireflies.lua edited (the
        # serval walks twice as fast: SPEED, a constant, becomes `speed`, a
        # global set to 3.0, so Player:step shrinks and the code after it
        # moves) and built with KEEP_LAYOUT against the page's own build,
        # into this directory: fireflies.svmap, .svdbg, _script.c, which
        # svlink_e2e reloads into the running page (svlink-reload-*.png: the
        # frames before and after).
        file(READ "${PROJECT_SOURCE_DIR}/examples/fireflies/fireflies.lua" reload_script)
        set(reload_edited "${reload_script}")
        foreach(edit "local SPEED <const> = 1.5|speed = 3.0" "vx = -SPEED|vx = -speed"
                     "vx = SPEED|vx = speed" "vy = -SPEED|vy = -speed" "vy = SPEED|vy = speed")
            string(REPLACE "|" ";" edit "${edit}")
            list(GET edit 0 from)
            list(GET edit 1 to)
            string(FIND "${reload_edited}" "${from}" at)
            if(at EQUAL -1)
                message(FATAL_ERROR "svlink_e2e's reload: fireflies.lua has no \"${from}\" any more")
            endif()
            string(REPLACE "${from}" "${to}" reload_edited "${reload_edited}")
        endforeach()
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/reload/fireflies.lua.new" "${reload_edited}")
        configure_file("${CMAKE_CURRENT_BINARY_DIR}/reload/fireflies.lua.new"
                       "${CMAKE_CURRENT_BINARY_DIR}/reload/fireflies.lua" COPYONLY)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                     "${PROJECT_SOURCE_DIR}/examples/fireflies/fireflies.lua")
        add_library(svlink_reload_fireflies OBJECT)
        serval_add_script(svlink_reload_fireflies "${CMAKE_CURRENT_BINARY_DIR}/reload/fireflies.lua"
                          KEEP_LAYOUT "${examples}/fireflies.svdbg"
                          HEADERS ../examples/fireflies/game.h serval/ecs.h serval/core.h
                                  serval/sprites.h serval/path.h serval/screen.h)
        add_dependencies(svlink_reload_fireflies fireflies)
        add_test(NAME svlink_e2e
            COMMAND "${SERVAL_PYTHON_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/svlink_test.py"
                    --page "${examples}/fireflies.html"
                    --release-page "${svlink_release}/examples/fireflies.html"
                    --script-c "${examples}/fireflies_script.c" --script-h "${examples}/fireflies_script.h"
                    --svmap "${examples}/fireflies.svmap" --version "${SERVAL_VERSION_STRING}"
                    --reload "${CMAKE_CURRENT_BINARY_DIR}/fireflies.svmap"
                    --shots "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(svlink_e2e PROPERTIES FIXTURES_REQUIRED svlink_release
                             SKIP_RETURN_CODE 77 TIMEOUT 600)
    endif()
    return()
endif()
add_test(NAME planned_api COMMAND ${_serval_planned_check})

add_library(serval_test_harness STATIC test.c ecs_tests.c random_tests.c text_format_tests.c math_tests.c
    physics_tests.c map_tests.c anim_tests.c psg_sequencer_tests.c psg_wave_tests.c save_tests.c input_tests.c path_tests.c splash_logic_tests.c
    vm_tests.c color_tests.c planned_audio_tests.c planned_sprites_tests.c planned_map_tests.c
    planned_screen_tests.c
    map_slope_tests.c) # ladders and slopes
target_link_libraries(serval_test_harness
    PUBLIC serval_portable
    PRIVATE serval_warnings)
target_sources(serval_test_harness PRIVATE lz77_tests.c) # lz77: the loaders' check
target_sources(serval_test_harness PRIVATE planned_debug_tests.c) # debug link: debug_break()
# ladders and slopes: a script climbing a ladder by MAP_CONTACT_LADDER (map_slope_tests.c)
serval_add_script(serval_test_harness ladder_climber.lua
                  HEADERS serval/map.h serval/ecs.h serval/physics.h)

if(SERVAL_TARGET_GBA)
    serval_add_rom(serval_tests
        SOURCES rom/main.c rom/core_tests.c rom/libc_tests.c rom/sprite_tests.c rom/gba_text_tests.c rom/audio_tests.c rom/splash_tests.c rom/map_tests.c rom/present_tests.c rom/ecs_cost_tests.c
                rom/vm_platform_tests.c
                rom/map_movement_cost_tests.c # ladders and slopes
                rom/sprite_tiles_tests.c
                rom/blend_tests.c
                rom/palette_tests.c
                rom/sprite_stream_tests.c
                rom/raster_tests.c
                rom/wave_tests.c
                rom/vm_sound_tests.c
                rom/compat_tonc_first.c rom/compat_serval_first.c
        TITLE "SERVAL TESTS"
        GAME_CODE "SVTS")
    target_link_libraries(serval_tests PRIVATE serval_test_harness serval_warnings)
    # lz77: the loaders against plain tiles: the tests' sprite and the
    # platformer's castle tileset and dragon, packed at build time
    target_sources(serval_tests PRIVATE rom/lz77_tests.c)
    serval_add_lz77(serval_tests lz77_tiles lz77/padded.inc
                    ../examples/platformer/castle_tiles.inc ../examples/platformer/dragon_tiles.inc)
    # debug link: the agent stays dormant without an endpoint (rom/link_tests.c);
    # and what the examples' maps show of it: in debug builds its core and no
    # IWRAM, its VM part only with the VM (fireflies, not hello); in release
    # builds, or with SERVAL_LINK off, none of it (rom/check_link_map.py).
    set(link_agent "$<AND:$<BOOL:${SERVAL_LINK}>,$<CONFIG:Debug,RelWithDebInfo>>")
    target_sources(serval_tests PRIVATE rom/link_tests.c)
    target_sources(serval_tests PRIVATE rom/link_reload_tests.c) # debug link (E9): a reload's cost
    target_compile_definitions(serval_tests PRIVATE "$<${link_agent}:SERVAL_LINK_AGENT>")
    if(SERVAL_BUILD_EXAMPLES)
        set(link_roms hello fireflies)
        set(link_vms off on)
        foreach(rom vm IN ZIP_LISTS link_roms link_vms)
            add_test(NAME link_map_${rom}
                COMMAND "${SERVAL_PYTHON_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/rom/check_link_map.py"
                        "$<TARGET_FILE_DIR:${rom}>/${rom}.map" --agent "$<IF:${link_agent},on,off>"
                        --vm ${vm} --scratch ${SERVAL_LINK_SCRATCH})
        endforeach()
    endif()

    # maxmod: tracker music and sampled sound effects, in a test ROM of their
    # own (tests/rom/sampled_audio_main.c: Maxmod's IWRAM doesn't fit beside
    # the other suites'), with a sound bank of modules and samples made by
    # tools/make-example-audio.py.
    serval_add_rom(serval_tests_sampled_audio
        SOURCES rom/sampled_audio_main.c rom/sampled_audio_tests.c
                rom/sampled_audio_vm_tests.c # vm sound: a script's calls with a bank
        TITLE "SV AUDIO"
        GAME_CODE "SVTA")
    target_link_libraries(serval_tests_sampled_audio PRIVATE serval_test_harness serval_warnings)
    serval_add_rom_checks(serval_tests_sampled_audio)
    # Every music_*() and sfx_*() call without a bank, as scripted games make
    # them: no Maxmod linked (rom/check-no-maxmod.cmake reads the map).
    serval_add_rom(serval_sampled_audio_calls SOURCES rom/sampled_audio_calls_main.c
        TITLE "SV SFX CALLS" GAME_CODE "SVTC")
    target_link_libraries(serval_sampled_audio_calls PRIVATE serval_warnings)
    serval_add_rom_checks(serval_sampled_audio_calls)
    add_test(NAME sampled_audio_calls_link_no_maxmod
        COMMAND "${CMAKE_COMMAND}"
                "-DMAP=$<TARGET_FILE_DIR:serval_sampled_audio_calls>/serval_sampled_audio_calls.map"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/rom/check-no-maxmod.cmake")
    set(test_audio_dir "${CMAKE_CURRENT_BINARY_DIR}/test_audio")
    set(test_audio tone.mod wide.mod beep.wav hum.wav)
    list(TRANSFORM test_audio PREPEND "${test_audio_dir}/")
    add_custom_command(
        OUTPUT ${test_audio}
        COMMAND "${SERVAL_PYTHON_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/make-example-audio.py"
                tests "${test_audio_dir}"
        DEPENDS "${PROJECT_SOURCE_DIR}/tools/make-example-audio.py"
        COMMENT "Making the sampled audio tests' modules and samples"
        VERBATIM)
    serval_add_soundbank(serval_tests_sampled_audio test_bank ${test_audio})
    # vm sound: a script naming the bank's modules and samples, through the
    # VM's SYS calls (rom/sampled_audio_vm_tests.c)
    serval_add_script(serval_tests_sampled_audio rom/sampled_audio_vm.lua
                      HEADERS "${CMAKE_CURRENT_BINARY_DIR}/test_bank.h" serval/core.h)

    # One more test ROM per save type: the save suites, on the ROM's SRAM,
    # Flash or EEPROM (tests/rom/save_main.c). debug link: SRAM's too, since
    # the debug link's agent (its mailbox, in every debug build) left no room
    # for the save suites' 157 KB of EWRAM in serval_tests.
    set(save_test_types SRAM FLASH64K FLASH128K EEPROM8K EEPROM512)
    set(save_test_codes SVSR SVF6 SVF1 SVE8 SVE5)
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
                    "-DREJECT=Detected (Flash|EEPROM)"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/rom/run-rom-test.cmake")
        # About 26 s in a Debug build; under a busy machine's load it has taken
        # over 60, so it gets the other test ROMs' 120.
        set_tests_properties(rom_tests PROPERTIES TIMEOUT 120)
        add_test(NAME rom_tests_sampled_audio # maxmod:
            COMMAND "${CMAKE_COMMAND}" "-DMGBA_ROM_TEST=${MGBA_ROM_TEST}"
                    "-DROM=$<TARGET_FILE_DIR:serval_tests_sampled_audio>/serval_tests_sampled_audio.gba"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/rom/run-rom-test.cmake")
        set_tests_properties(rom_tests_sampled_audio PROPERTIES TIMEOUT 120)
        add_test(NAME rom_tests_sampled_audio_calls # maxmod:
            COMMAND "${CMAKE_COMMAND}" "-DMGBA_ROM_TEST=${MGBA_ROM_TEST}"
                    "-DROM=$<TARGET_FILE_DIR:serval_sampled_audio_calls>/serval_sampled_audio_calls.gba"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/rom/run-rom-test.cmake")
        set(expect_SRAM "Detected SRAM savegame")
        set(reject_SRAM "Detected (Flash|EEPROM)")
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

    # script benchmark: scriptmark_bench and fireflies_bench (tools/bench.sh)
    add_subdirectory(bench)

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
    # The web backend's renderer, sound and mixer are plain C, tested
    # natively on arrays standing in for GBA memory.
    add_executable(serval_host_tests host/main.c web_ppu_tests.c web_apu_tests.c
                   web_mixer_tests.c ../src/web/ppu.c ../src/web/apu.c ../src/web/mixer.c)
    target_link_libraries(serval_host_tests PRIVATE serval_test_harness serval_warnings)
    add_test(NAME host_tests COMMAND serval_host_tests)
    # lz77: the web's LZ77 decoder, on BIOS-format vectors and on the
    # encoder's output (tools/lz77.py, which has its own tests)
    target_sources(serval_host_tests PRIVATE web_lz77_tests.c ../src/web/lz77.c)
    serval_add_lz77(serval_host_tests lz77_tiles lz77/padded.inc
                    ../examples/platformer/castle_tiles.inc ../examples/platformer/dragon_tiles.inc)
    # debug link: the agent against a fake endpoint (link_tests.c), with a
    # script that reads every property as GETP does (link_probe.lua)
    target_sources(serval_host_tests PRIVATE link_tests.c)
    target_compile_definitions(serval_host_tests PRIVATE
        "$<$<AND:$<BOOL:${SERVAL_LINK}>,$<CONFIG:Debug,RelWithDebInfo>>:SERVAL_LINK_AGENT>"
        "SERVAL_LINK_SCRATCH=${SERVAL_LINK_SCRATCH}"
        "SERVAL_LINK_ENGINE_VERSION=\"${SERVAL_VERSION_STRING}\"")
    serval_add_script(serval_host_tests link_probe.lua PREFIX PROBE_ HEADERS serval/ecs.h)
    # debug link (E6): the agent's VM part against the fake endpoint
    # (link_vm_tests.c), on link_debug.lua, whose sites' pcs link_sites.py
    # reads from the maps serval_add_script() writes, as a debugger does
    target_sources(serval_host_tests PRIVATE link_vm_tests.c)
    serval_add_script(serval_host_tests link_debug.lua PREFIX DEBUG_ HEADERS serval/ecs.h)
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/link_debug_sites.h"
        COMMAND "${SERVAL_PYTHON_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/link_sites.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/link_debug.lua"
                "${CMAKE_CURRENT_BINARY_DIR}/link_debug.svmap" "${CMAKE_CURRENT_BINARY_DIR}/link_debug.svdbg"
                -o "${CMAKE_CURRENT_BINARY_DIR}/link_debug_sites.h"
        DEPENDS link_debug.lua link_sites.py "${PROJECT_SOURCE_DIR}/tools/check-debug-maps.py"
                "${CMAKE_CURRENT_BINARY_DIR}/link_debug.svmap" "${CMAKE_CURRENT_BINARY_DIR}/link_debug.svdbg"
        COMMENT "Finding link_debug.lua's breakpoint sites"
        VERBATIM)
    target_sources(serval_host_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/link_debug_sites.h")
    # debug link (E9): the agent's hot reload against the fake endpoint
    # (link_reload_tests.c): a script and its edit, the edit built twice,
    # without and with KEEP_LAYOUT (the first's .svdbg), and the plans between
    # them, planned by tools/svlink.py (link_reload_plan.py) as a debugger does
    target_sources(serval_host_tests PRIVATE link_reload_tests.c)
    serval_add_script(serval_host_tests link_reload_v1.lua PREFIX R1_ HEADERS serval/ecs.h)
    serval_add_script(serval_host_tests link_reload_v2.lua PREFIX R2_ HEADERS serval/ecs.h)
    configure_file(link_reload_v2.lua "${CMAKE_CURRENT_BINARY_DIR}/link_reload_v2k.lua" COPYONLY)
    serval_add_script(serval_host_tests "${CMAKE_CURRENT_BINARY_DIR}/link_reload_v2k.lua"
                      KEEP_LAYOUT "${CMAKE_CURRENT_BINARY_DIR}/link_reload_v1.svdbg"
                      PREFIX R2K_ HEADERS serval/ecs.h)
    set(reload_builds "")
    foreach(v v1 v2 v2k)
        set(base "${CMAKE_CURRENT_BINARY_DIR}/link_reload_${v}")
        list(APPEND reload_builds "${base}.svmap" "${base}.svdbg" "${base}_script.c")
    endforeach()
    set(base "${CMAKE_CURRENT_BINARY_DIR}/link_reload")
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/link_reload_plans.h"
        COMMAND "${SERVAL_PYTHON_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/link_reload_plan.py"
                -o "${CMAKE_CURRENT_BINARY_DIR}/link_reload_plans.h"
                "V2=${base}_v1:${base}_v2" "V2K=${base}_v1:${base}_v2k" "SAME=${base}_v2:${base}_v2"
                "K2=${base}_v2k:${base}_v2"
        DEPENDS link_reload_plan.py "${PROJECT_SOURCE_DIR}/tools/svlink.py"
                "${PROJECT_SOURCE_DIR}/tools/svm.py" ${reload_builds}
        COMMENT "Planning the hot reload tests' reloads"
        VERBATIM)
    target_sources(serval_host_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/link_reload_plans.h")
    # debug link: tools/svlink.py, the link's reference client, without a
    # browser: the codec, the WebSocket server, its checks (svlink_e2e, in web
    # builds, runs it against a page)
    add_test(NAME svlink_tool
        COMMAND "${SERVAL_PYTHON_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/svlink_test.py")
    add_test(NAME lz77_tool
        COMMAND "${SERVAL_PYTHON_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/lz77_test.py")

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
    # The sound bank builder (tools/soundbank.py): mmutil's names, what it
    # refuses (a sample named "none" among them), and a whole bank with the
    # mmutil serval_add_soundbank() found.
    add_test(NAME soundbank_tool
        COMMAND "${CMAKE_COMMAND}" -E env "SERVAL_MMUTIL=${SERVAL_MMUTIL}"
                "${SERVAL_PYTHON_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/soundbank_test.py")
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
    # debug link: the maps serval_add_script() wrote beside the scripts this
    # build compiles (.svmap, and .svdbg for Lua), joined as a debugger joins
    # them and checked against the blobs and listings (docs/lua.md#debug-information).
    set(_serval_debug_maps consumer fireflies ladder_climber pads soundbank_names svm/fireflies)
    list(TRANSFORM _serval_debug_maps PREPEND "${CMAKE_CURRENT_BINARY_DIR}/")
    add_test(NAME debug_maps
        COMMAND "${SERVAL_PYTHON_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/check-debug-maps.py"
                ${_serval_debug_maps})
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
    # The jukebox example includes its sound bank's header: the bank is built
    # for this check too (examples/CMakeLists.txt).
    set(jukebox_audio theme.mod calm.mod coin.wav laser.wav boom.wav engine.wav)
    list(TRANSFORM jukebox_audio PREPEND "${CMAKE_CURRENT_BINARY_DIR}/jukebox_audio/")
    add_custom_command(
        OUTPUT ${jukebox_audio}
        COMMAND "${SERVAL_PYTHON_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/make-example-audio.py"
                jukebox "${CMAKE_CURRENT_BINARY_DIR}/jukebox_audio"
        DEPENDS "${PROJECT_SOURCE_DIR}/tools/make-example-audio.py"
        COMMENT "Making the jukebox's modules and samples"
        VERBATIM)
    serval_add_soundbank(serval_api_only_check jukebox_bank ${jukebox_audio})
    # Scripts name a bank's modules and samples from its generated header.
    serval_add_script(serval_api_only_check soundbank_names.lua
                      HEADERS "${CMAKE_CURRENT_BINARY_DIR}/jukebox_bank.h")
    # vm sound: the jukebox's main.c includes its script's header
    serval_add_script(serval_api_only_check ../examples/jukebox/pads.lua
                      HEADERS "${CMAKE_CURRENT_BINARY_DIR}/jukebox_bank.h" serval/core.h)
    # lz77: the platformer's castle and consumer include their LZ77 data's
    # headers (examples/CMakeLists.txt, consumer/CMakeLists.txt).
    serval_add_lz77(serval_api_only_check castle_lz77 ../examples/platformer/castle_tiles.inc
                    ../examples/platformer/dragon_tiles.inc)
    serval_add_lz77(serval_api_only_check consumer_lz77 consumer/consumer_tiles.inc)
    # A hand-written listing (.svm) through serval_add_script() too.
    add_subdirectory(svm)
    add_dependencies(serval_host_tests serval_svm_listing_check)

    # Every public header compiles on its own (includes what it uses).
    serval_add_header_check(serval_header_check)
    add_dependencies(serval_host_tests serval_header_check)
endif()
