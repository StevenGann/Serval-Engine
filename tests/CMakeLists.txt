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
    physics_tests.c map_tests.c anim_tests.c psg_sequencer_tests.c save_tests.c)
target_link_libraries(serval_test_harness
    PUBLIC serval_portable
    PRIVATE serval_warnings)

if(SERVAL_TARGET_GBA)
    serval_add_rom(serval_tests
        SOURCES rom/main.c rom/core_tests.c rom/libc_tests.c rom/sprite_tests.c rom/gba_text_tests.c rom/audio_tests.c rom/splash_tests.c rom/map_tests.c rom/present_tests.c rom/save_tests.c
                rom/compat_tonc_first.c rom/compat_serval_first.c
        TITLE "SERVAL TESTS"
        GAME_CODE "SVTS")
    target_link_libraries(serval_tests PRIVATE serval_test_harness serval_warnings)

    # mgba-rom-test runs the ROM headless until SWI 3 and exits with r0: 0 if
    # every check passed, 1 otherwise. Build it with tools/build-mgba-rom-test.sh.
    find_program(MGBA_ROM_TEST mgba-rom-test HINTS ENV MGBA_ROM_TEST_DIR)
    if(MGBA_ROM_TEST)
        add_test(NAME rom_tests
            COMMAND "${MGBA_ROM_TEST}" -S 3 -R r0 -l 15
                    "$<TARGET_FILE_DIR:serval_tests>/serval_tests.gba")
        set_tests_properties(rom_tests PROPERTIES TIMEOUT 60)
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
        consumer/main.c)
    target_include_directories(serval_api_only_check PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(serval_api_only_check PRIVATE serval_warnings)
    add_dependencies(serval_host_tests serval_api_only_check)

    # Every public header compiles on its own (includes what it uses).
    serval_add_header_check(serval_header_check)
    add_dependencies(serval_host_tests serval_header_check)
endif()
