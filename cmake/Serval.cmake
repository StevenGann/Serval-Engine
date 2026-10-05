# Shared build settings and the serval_add_rom() helper.
#
# This file is included from the engine's top-level CMakeLists.txt, which a
# game project may reach through add_subdirectory(). Everything
# serval_add_rom() needs must therefore work from any directory: paths come
# from CMAKE_CURRENT_FUNCTION_LIST_DIR and settings from cache variables or
# target properties, never from variables of the engine's directory scope.

# Warning flags for engine code (not vendored third-party code). Link PRIVATE.
add_library(serval_warnings INTERFACE)
target_compile_options(serval_warnings INTERFACE
    $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -Wshadow -Wundef -Wstrict-prototypes -Wmissing-prototypes>)
if(SERVAL_WARNINGS_AS_ERRORS)
    target_compile_options(serval_warnings INTERFACE $<$<COMPILE_LANGUAGE:C>:-Werror>)
endif()

if(SERVAL_TARGET_GBA)
    # Put every function and object in its own section so --gc-sections can
    # drop what a game does not use. This covers the engine and libtonc; game
    # code gets the same flags from the serval target (CMakeLists.txt).
    add_compile_options(-ffunction-sections -fdata-sections)

    # gbafix.py fixes and pads every ROM. Cached so that serval_add_rom() finds
    # it when called from a game's own directory.
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(SERVAL_PYTHON_EXECUTABLE "${Python3_EXECUTABLE}" CACHE INTERNAL
        "Python interpreter that runs tools/gbafix.py")

    # libgcc (integer division and other helpers) must come last on the link
    # line, after every library that may need it. The toolchain file sets this
    # up; repair it here if another toolchain file was used. Cached, because a
    # normal variable would only reach targets in the engine's directories.
    if(NOT CMAKE_C_STANDARD_LIBRARIES MATCHES "(^| )-lgcc( |$)")
        set(CMAKE_C_STANDARD_LIBRARIES "${CMAKE_C_STANDARD_LIBRARIES} -lgcc" CACHE STRING
            "Libraries linked by default with all C applications." FORCE)
    endif()
endif()

# serval_add_rom(<target> SOURCES <files...> [TITLE <title>] [GAME_CODE <code>]
#                [SAVE SRAM|FLASH64K|FLASH128K|EEPROM8K|EEPROM512])
#
# Builds <target>.elf from the given sources linked against the engine, then
# converts it to <target>.gba and fixes the ROM header. The header never
# contains Nintendo's logo (see docs/licensing.md).
#
# SAVE is the cartridge save memory the game's save data (save.h) uses: SRAM
# (32 KiB, the default), FLASH64K, FLASH128K, EEPROM8K or EEPROM512. It sets
# the number of slots and their capacity (docs/runtime-systems.md#save-data).
# Only that type's code and ROM ID string are linked, and none of it if the
# game never calls save_*.
#
# In web builds (the web preset), builds <target>.html instead: the game as one
# self-contained page (cmake/ServalWeb.cmake). TITLE and GAME_CODE name its
# saves in the browser's localStorage there; SAVE gives it the same slots as
# on the GBA.
function(serval_add_rom target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "TITLE;GAME_CODE;SAVE" "SOURCES")
    if(NOT ARG_TITLE)
        string(TOUPPER "${target}" ARG_TITLE)
    endif()
    if(NOT ARG_GAME_CODE)
        set(ARG_GAME_CODE "0000")
    endif()
    if(NOT DEFINED ARG_SAVE)
        set(ARG_SAVE "SRAM")
    endif()
    set(save_types SRAM FLASH64K FLASH128K EEPROM8K EEPROM512)
    if(NOT ARG_SAVE IN_LIST save_types)
        list(JOIN save_types ", " save_types)
        message(FATAL_ERROR "serval_add_rom(${target}): SAVE ${ARG_SAVE} is not a save type; "
                            "use one of ${save_types}.")
    endif()
    string(TOLOWER "${ARG_SAVE}" save_name)
    if(NOT TARGET serval_save_${save_name})
        message(FATAL_ERROR "serval_add_rom(${target}): the engine is not configured "
                            "(serval_save_${save_name} is missing).")
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        if(NOT TARGET serval)
            message(FATAL_ERROR "serval_add_rom(${target}): the engine is not configured.")
        endif()
        _serval_add_web_page(${target} "${ARG_TITLE}" "${ARG_GAME_CODE}"
                             ${ARG_SOURCES} $<TARGET_OBJECTS:serval_save_${save_name}>)
        set_target_properties(${target} PROPERTIES SERVAL_ROM_SAVE_TYPE "${ARG_SAVE}")
        return()
    endif()

    if(NOT TARGET serval OR NOT TARGET serval_crt0)
        message(FATAL_ERROR "serval_add_rom(${target}): the GBA engine is not configured. "
                            "Configure with cmake/arm-gba-toolchain.cmake as the toolchain file.")
    endif()
    set(engine_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
    set(linker_script "${engine_dir}/src/gba/gba.ld")
    set(gbafix "${engine_dir}/tools/gbafix.py")
    foreach(file IN ITEMS "${linker_script}" "${gbafix}")
        if(NOT EXISTS "${file}")
            message(FATAL_ERROR "serval_add_rom(${target}): ${file} is missing.")
        endif()
    endforeach()
    if(NOT SERVAL_PYTHON_EXECUTABLE OR NOT SERVAL_OBJCOPY)
        message(FATAL_ERROR "serval_add_rom(${target}): Python 3 and arm-none-eabi-objcopy are "
                            "needed to create the ROM (SERVAL_PYTHON_EXECUTABLE="
                            "'${SERVAL_PYTHON_EXECUTABLE}', SERVAL_OBJCOPY='${SERVAL_OBJCOPY}').")
    endif()

    # The save memory object goes in like crt0 and libc: as an object, so it
    # defines the device save.c (in the serval archive) uses; --gc-sections
    # drops it with save.c when the game never saves.
    add_executable(${target} ${ARG_SOURCES} $<TARGET_OBJECTS:serval_crt0>
                   $<TARGET_OBJECTS:serval_libc> $<TARGET_OBJECTS:serval_save_${save_name}>)
    set_target_properties(${target} PROPERTIES
        SUFFIX ".elf"
        SERVAL_ROM_TITLE "${ARG_TITLE}"
        SERVAL_ROM_GAME_CODE "${ARG_GAME_CODE}"
        SERVAL_ROM_SAVE_TYPE "${ARG_SAVE}")
    target_link_libraries(${target} PRIVATE serval)
    # No C library: the engine provides the few routines GCC may call
    # (src/gba/libc.c) so newlib is never linked.
    target_link_options(${target} PRIVATE
        -nostartfiles -nostdlib
        "-T${linker_script}"
        -Wl,--gc-sections
        # IWRAM holds both code and data, so its segment is RWX by design.
        -Wl,--no-warn-rwx-segments
        "-Wl,-Map=$<TARGET_FILE_DIR:${target}>/${target}.map")
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${linker_script}")

    set(rom "$<TARGET_FILE_DIR:${target}>/${target}.gba")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${SERVAL_OBJCOPY}" -O binary "$<TARGET_FILE:${target}>" "${rom}"
        COMMAND "${SERVAL_PYTHON_EXECUTABLE}" "${gbafix}" "${rom}"
                --title "${ARG_TITLE}" --game-code "${ARG_GAME_CODE}"
        COMMENT "Creating ${target}.gba"
        VERBATIM)
endfunction()

include("${CMAKE_CURRENT_LIST_DIR}/ServalWeb.cmake")
