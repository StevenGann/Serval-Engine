add_library(serval_test_harness STATIC test.c ecs_tests.c random_tests.c text_format_tests.c math_tests.c
    physics_tests.c)
target_link_libraries(serval_test_harness
    PUBLIC serval_portable
    PRIVATE serval_warnings)

if(SERVAL_TARGET_GBA)
    serval_add_rom(serval_tests
        SOURCES rom/main.c rom/core_tests.c rom/libc_tests.c rom/sprite_tests.c rom/gba_text_tests.c
                rom/compat_tonc_first.c rom/compat_serval_first.c
        TITLE "SERVAL TESTS"
        GAME_CODE "SVTS")
    target_link_libraries(serval_tests PRIVATE serval_test_harness serval_warnings)

    # mgba-rom-test runs the ROM headless until SWI 3 and exits with r0, the
    # number of failed checks. Build it with tools/build-mgba-rom-test.sh.
    find_program(MGBA_ROM_TEST mgba-rom-test HINTS ENV MGBA_ROM_TEST_DIR)
    if(MGBA_ROM_TEST)
        add_test(NAME rom_tests
            COMMAND "${MGBA_ROM_TEST}" -S 3 -R r0 -l 15
                    "$<TARGET_FILE_DIR:serval_tests>/serval_tests.gba")
        set_tests_properties(rom_tests PROPERTIES TIMEOUT 60)
    else()
        message(STATUS "mgba-rom-test not found: ROM tests are built but not run")
    endif()

    # ROMs of 256 KiB or less can be mistaken for multiboot images by older
    # emulators (white screen in mGBA 0.8); tools/gbafix.py pads them.
    add_test(NAME rom_is_never_multiboot_sized
        COMMAND "${Python3_EXECUTABLE}" -c
            "import os, sys; size = os.path.getsize(sys.argv[1]); print(size); sys.exit(size <= 256 * 1024)"
            "$<TARGET_FILE_DIR:serval_tests>/serval_tests.gba")
else()
    add_executable(serval_host_tests host/main.c)
    target_link_libraries(serval_host_tests PRIVATE serval_test_harness serval_warnings)
    add_test(NAME host_tests COMMAND serval_host_tests)

    # Games must never need a third-party header. libtonc is not on the host
    # include path, so these compile only if the public headers and the
    # examples use nothing but Serval's API.
    add_library(serval_api_only_check OBJECT
        public_headers.c
        "${PROJECT_SOURCE_DIR}/examples/hello/main.c"
        "${PROJECT_SOURCE_DIR}/examples/bunnymark/main.c"
        "${PROJECT_SOURCE_DIR}/examples/bunnymark/bunnymark.c"
        "${PROJECT_SOURCE_DIR}/examples/bunnymark/bench.c")
    target_include_directories(serval_api_only_check PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(serval_api_only_check PRIVATE serval_warnings)
    add_dependencies(serval_host_tests serval_api_only_check)
endif()
