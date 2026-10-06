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

add_library(serval_test_harness STATIC test.c ecs_tests.c random_tests.c text_format_tests.c math_tests.c
    physics_tests.c map_tests.c anim_tests.c psg_sequencer_tests.c save_tests.c input_tests.c path_tests.c
    vm_tests.c)
target_link_libraries(serval_test_harness
    PUBLIC serval_portable
    PRIVATE serval_warnings)

if(SERVAL_TARGET_GBA)
    serval_add_rom(serval_tests
        SOURCES rom/main.c rom/core_tests.c rom/libc_tests.c rom/sprite_tests.c rom/gba_text_tests.c rom/audio_tests.c rom/splash_tests.c rom/map_tests.c rom/present_tests.c rom/save_tests.c rom/ecs_cost_tests.c
                rom/vm_platform_tests.c
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

    # Games must never need a third-party header. libtonc is not on the host
    # include path, so these compile only if the public headers and the
    # examples use nothing but Serval's API.
    file(GLOB _serval_example_sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/examples/*/*.c")
    add_library(serval_api_only_check OBJECT public_headers.c ${_serval_example_sources}
        consumer/main.c consumer/save.c)
    target_include_directories(serval_api_only_check PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(serval_api_only_check PRIVATE serval_warnings)
    add_dependencies(serval_host_tests serval_api_only_check)

    # Every public header compiles on its own (includes what it uses).
    serval_add_header_check(serval_header_check)
    add_dependencies(serval_host_tests serval_header_check)
endif()
