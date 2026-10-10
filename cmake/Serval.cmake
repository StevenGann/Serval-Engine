# Shared build settings and the serval_add_rom(), serval_add_script() and
# serval_add_soundbank() helpers.
#
# This file is included from the engine's top-level CMakeLists.txt, which a
# game project may reach through add_subdirectory(). Everything the helpers
# need must therefore work from any directory: paths come from
# CMAKE_CURRENT_FUNCTION_LIST_DIR and settings from cache variables or target
# properties, never from variables of the engine's directory scope.

# Python runs tools/gbafix.py after each ROM link, and tools/svlua.py and
# tools/svm.py for scripts, in every kind of build (GBA, web and host). Cached
# so that the helpers find it when called from a game's own directory.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(SERVAL_PYTHON_EXECUTABLE "${Python3_EXECUTABLE}" CACHE INTERNAL
    "Python interpreter that runs the engine's tools (gbafix.py, svlua.py, svm.py)")
set(SERVAL_PYTHON_VERSION "${Python3_VERSION}" CACHE INTERNAL
    "Version of SERVAL_PYTHON_EXECUTABLE (svlua.py needs 3.11 or later)")

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

    # libgcc (integer division and other helpers) must come last on the link
    # line, after every library that may need it. The toolchain file sets this
    # up; repair it here if another toolchain file was used. Cached, because a
    # normal variable would only reach targets in the engine's directories.
    if(NOT CMAKE_C_STANDARD_LIBRARIES MATCHES "(^| )-lgcc( |$)")
        set(CMAKE_C_STANDARD_LIBRARIES "${CMAKE_C_STANDARD_LIBRARIES} -lgcc" CACHE STRING
            "Libraries linked by default with all C applications." FORCE)
    endif()
endif()

# _serval_bad_character(<out_var> <text>)
#
# Sets <out_var> to the first character of <text> that is not printable ASCII
# (0x20 space to 0x7E tilde), in quotes, or to "a control character"; to an
# empty string if every character is printable ASCII.
function(_serval_bad_character out_var text)
    set(description "")
    if(NOT text MATCHES "^[ -~]*$")
        string(REGEX MATCH "^[ -~]*" good "${text}")
        string(LENGTH "${good}" at)
        string(SUBSTRING "${text}" ${at} 1 byte)
        string(HEX "${byte}" hex)
        if(hex STRLESS "20" OR hex STREQUAL "7f")
            set(description "a control character")
        else()
            # Not ASCII: the whole UTF-8 sequence, lead byte and continuation
            # bytes, so the message shows the character.
            set(length 1)
            if(hex STRGREATER_EQUAL "f0")
                set(length 4)
            elseif(hex STRGREATER_EQUAL "e0")
                set(length 3)
            elseif(hex STRGREATER_EQUAL "c0")
                set(length 2)
            endif()
            string(SUBSTRING "${text}" ${at} ${length} character)
            set(description "\"${character}\"")
        endif()
    endif()
    set(${out_var} "${description}" PARENT_SCOPE)
endfunction()

# serval_add_rom(<target> SOURCES <files...> [TITLE <title>] [GAME_CODE <code>]
#                [SAVE SRAM|FLASH64K|FLASH128K|EEPROM8K|EEPROM512])
#
# Builds <target>.elf from the given sources linked against the engine, then
# converts it to <target>.gba and fixes the ROM header. The header never
# contains Nintendo's logo (see docs/licensing.md).
#
# TITLE is the header's title: 1 to 12 printable ASCII characters (space to
# tilde). Left out, it is the target name in upper case, cut to 12
# characters. GAME_CODE is the header's game code: exactly 4 printable ASCII
# characters, 0000 if left out. Any printable ASCII character works, spaces
# at either end included, and reaches the header and the web page unchanged.
# Anything else stops the configuration with an error: an empty TITLE, a
# longer one, a GAME_CODE of any other length (it is never padded), a control
# or non-ASCII character in either.
#
# SAVE is the cartridge save memory the game's save data (save.h) uses: SRAM
# (32 KiB, the default), FLASH64K, FLASH128K, EEPROM8K or EEPROM512. It sets
# the number of slots and their capacity (docs/runtime-systems.md#save-data).
# Only that type's code and ROM ID string are linked, and none of it if the
# game never calls save_*. Any other value, an empty one included, is an
# error.
#
# In web builds (the web preset), builds <target>.html instead: the game as one
# self-contained page (cmake/ServalWeb.cmake). TITLE is the page's title, and
# TITLE and GAME_CODE name its saves in the browser's localStorage; SAVE gives
# it the same slots as on the GBA.
function(serval_add_rom target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "TITLE;GAME_CODE;SAVE" "SOURCES")
    # Which of TITLE, GAME_CODE and SAVE were given, with a value or not:
    # cmake_parse_arguments() leaves ARG_TITLE undefined both when TITLE is
    # left out and when its value is empty (unless policy CMP0174 is NEW, in
    # CMake 3.31 and later), and an empty value must be refused, not replaced
    # by the default. (Not if(NOT ARG_TITLE) either: that is true for titles
    # such as OFF, NO or 0.)
    set(given "")
    if(ARGC GREATER 1)
        math(EXPR last "${ARGC} - 1")
        foreach(i RANGE 1 ${last})
            if("${ARGV${i}}" MATCHES "^(TITLE|GAME_CODE|SAVE)$")
                list(APPEND given "${ARGV${i}}")
            endif()
        endforeach()
    endif()
    if(NOT "TITLE" IN_LIST given)
        string(TOUPPER "${target}" ARG_TITLE)
        string(SUBSTRING "${ARG_TITLE}" 0 12 ARG_TITLE)
    elseif(NOT DEFINED ARG_TITLE)
        set(ARG_TITLE "")
    endif()
    if(NOT "GAME_CODE" IN_LIST given)
        set(ARG_GAME_CODE "0000")
    elseif(NOT DEFINED ARG_GAME_CODE)
        set(ARG_GAME_CODE "")
    endif()
    if(NOT "SAVE" IN_LIST given)
        set(ARG_SAVE "SRAM")
    elseif(NOT DEFINED ARG_SAVE)
        set(ARG_SAVE "")
    endif()

    # The rules Studio Advance applies to the same fields.
    if(ARG_TITLE STREQUAL "")
        message(FATAL_ERROR "serval_add_rom(${target}): TITLE is empty; give 1 to 12 printable "
                            "ASCII characters, or leave TITLE out to use the target name.")
    endif()
    _serval_bad_character(bad "${ARG_TITLE}")
    if(NOT bad STREQUAL "")
        message(FATAL_ERROR "serval_add_rom(${target}): TITLE can't contain ${bad}: use ASCII "
                            "letters, digits, spaces and punctuation.")
    endif()
    string(LENGTH "${ARG_TITLE}" length)
    if(length GREATER 12)
        message(FATAL_ERROR "serval_add_rom(${target}): TITLE can have at most 12 characters; "
                            "\"${ARG_TITLE}\" has ${length}.")
    endif()
    _serval_bad_character(bad "${ARG_GAME_CODE}")
    if(NOT bad STREQUAL "")
        message(FATAL_ERROR "serval_add_rom(${target}): GAME_CODE can't contain ${bad}: use "
                            "ASCII letters, digits, spaces and punctuation.")
    endif()
    string(LENGTH "${ARG_GAME_CODE}" length)
    if(NOT length EQUAL 4)
        message(FATAL_ERROR "serval_add_rom(${target}): GAME_CODE must have exactly 4 "
                            "characters; \"${ARG_GAME_CODE}\" has ${length}.")
    endif()
    set(save_types SRAM FLASH64K FLASH128K EEPROM8K EEPROM512)
    if(NOT ARG_SAVE IN_LIST save_types)
        list(JOIN save_types ", " save_types)
        message(FATAL_ERROR "serval_add_rom(${target}): SAVE \"${ARG_SAVE}\" is not a save type; "
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

    # The title and game code go to gbafix.py as hexadecimal character codes,
    # which nothing between here and gbafix.py changes. As text, they would be
    # evaluated as generator expressions (a TITLE of "$<1:X>" would become
    # "X") and would rely on every generator quoting every character right for
    # its shell (cmd.exe, for one, expands %NAME% even inside quotes).
    string(HEX "${ARG_TITLE}" title_hex)
    string(HEX "${ARG_GAME_CODE}" game_code_hex)
    set(rom "$<TARGET_FILE_DIR:${target}>/${target}.gba")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${SERVAL_OBJCOPY}" -O binary "$<TARGET_FILE:${target}>" "${rom}"
        COMMAND "${SERVAL_PYTHON_EXECUTABLE}" "${gbafix}" "${rom}"
                --title-hex ${title_hex} --game-code-hex ${game_code_hex}
        COMMENT "Creating ${target}.gba"
        VERBATIM)
endfunction()

# serval_add_script(<target> <script.lua | listing.svm> [SYMBOL <name>]
#                   [PREFIX <p>] [HEADERS <h1> <h2> ...])
#
# Builds a script blob for a target made by serval_add_rom() (or any target)
# at build time: a script in the Lua subset (.lua, docs/lua.md) is compiled
# by tools/svlua.py to <basename>.svm in the target's binary directory, and
# that listing, or a hand-written one (.svm, docs/vm.md), is assembled by
# tools/svm.py into <basename>_script.c, which defines the blob as
# `const unsigned char <symbol>[]` and `<symbol>_size`, and
# <basename>_script.h with the script's objects, strings, globals and arrays
# as <PREFIX>OBJ_*, <PREFIX>STR_*, <PREFIX>G_* and <PREFIX>ARR_* defines,
# their counts and the two externs. Both go in the target's binary
# directory; the .c joins the target's sources and the directory its include
# path, so the game includes the header and calls
# vm_load(<symbol>, <symbol>_size).
#
# SYMBOL defaults to <basename>_script. HEADERS are C headers whose integer
# constants the script may use (the game's, and the engine's: a path relative
# to the current source directory, or to the engine's include/ as the game
# would #include it, e.g. serval/ecs.h); a Lua script's names in ALL_CAPS
# come from them; a header generated in the same directory (a sound bank's,
# serval_add_soundbank(), with its MOD_* and SFX_*) may be named before it
# is built. The script is rebuilt when it, a header, svlua.py, svm.py
# or vm.h changes, and a Lua script when an engine header does (svlua.py reads
# the engine's functions, whose names scripts can't take, from them). Call it
# from the directory that defined the target, after serval_add_rom().
function(serval_add_script target listing)
    cmake_parse_arguments(PARSE_ARGV 2 ARG "" "SYMBOL;PREFIX" "HEADERS")
    if(NOT TARGET ${target})
        message(FATAL_ERROR "serval_add_script(${target}): no such target; call it after "
                            "serval_add_rom(${target} ...).")
    endif()
    set(engine_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
    set(svm "${engine_dir}/tools/svm.py")
    set(svlua "${engine_dir}/tools/svlua.py")
    set(vm_h "${engine_dir}/include/serval/vm.h")
    cmake_path(ABSOLUTE_PATH listing BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
    cmake_path(GET listing EXTENSION LAST_ONLY extension)
    string(TOLOWER "${extension}" extension)
    if(NOT extension MATCHES "^\\.(lua|svm)$")
        message(FATAL_ERROR "serval_add_script(${target}): ${listing} is neither a Lua script "
                            "(.lua) nor a listing (.svm).")
    endif()
    set(tools "${svm}" "${vm_h}")
    if(extension STREQUAL ".lua")
        list(APPEND tools "${svlua}")
    endif()
    foreach(file IN LISTS tools)
        if(NOT EXISTS "${file}")
            message(FATAL_ERROR "serval_add_script(${target}): ${file} is missing.")
        endif()
    endforeach()
    if(NOT SERVAL_PYTHON_EXECUTABLE)
        message(FATAL_ERROR "serval_add_script(${target}): Python 3 is needed to build "
                            "${listing} (SERVAL_PYTHON_EXECUTABLE is empty).")
    endif()
    if(NOT EXISTS "${listing}")
        message(FATAL_ERROR "serval_add_script(${target}): ${listing} does not exist.")
    endif()
    if(extension STREQUAL ".lua" AND SERVAL_PYTHON_VERSION VERSION_LESS 3.11)
        message(FATAL_ERROR "serval_add_script(${target}): compiling ${listing} needs Python "
                            "3.11 or later (tools/svlua.py); found ${SERVAL_PYTHON_VERSION} at "
                            "${SERVAL_PYTHON_EXECUTABLE}.")
    endif()
    cmake_path(GET listing STEM base)
    if(NOT ARG_SYMBOL)
        set(ARG_SYMBOL "${base}_script")
    endif()
    set(header_args "")
    set(headers "")
    foreach(header IN LISTS ARG_HEADERS)
        if(NOT IS_ABSOLUTE "${header}" AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${header}"
           AND EXISTS "${engine_dir}/include/${header}")
            set(header "${engine_dir}/include/${header}")
        endif()
        cmake_path(ABSOLUTE_PATH header BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
        # A header a custom command generates (serval_add_soundbank()'s)
        # exists only once built.
        get_source_file_property(generated "${header}" GENERATED)
        if(NOT EXISTS "${header}" AND NOT generated)
            message(FATAL_ERROR "serval_add_script(${target}): header ${header} does not exist.")
        endif()
        list(APPEND headers "${header}")
        list(APPEND header_args --header "${header}")
    endforeach()
    set(prefix_args "")
    if(ARG_PREFIX)
        set(prefix_args --prefix "${ARG_PREFIX}")
    endif()

    get_target_property(out_dir ${target} BINARY_DIR)
    set(c_file "${out_dir}/${base}_script.c")
    set(h_file "${out_dir}/${base}_script.h")
    if(extension STREQUAL ".lua")
        # The compiler's listing, which the assembler turns into the blob.
        set(script "${listing}")
        set(listing "${out_dir}/${base}.svm")
        file(GLOB engine_headers "${engine_dir}/include/serval/*.h")
        add_custom_command(
            OUTPUT "${listing}"
            COMMAND "${SERVAL_PYTHON_EXECUTABLE}" "${svlua}" compile "${script}" -o "${listing}"
            DEPENDS "${script}" "${svlua}" ${engine_headers}
            COMMENT "Compiling ${base}.lua"
            VERBATIM)
    endif()
    add_custom_command(
        OUTPUT "${c_file}" "${h_file}"
        COMMAND "${SERVAL_PYTHON_EXECUTABLE}" "${svm}" asm "${listing}" ${header_args}
                --c "${c_file}" --symbol "${ARG_SYMBOL}" --defs "${h_file}" ${prefix_args}
        DEPENDS "${listing}" ${headers} "${svm}" "${vm_h}"
        COMMENT "Assembling ${base}.svm"
        VERBATIM)
    # The header is listed as a source too, so every object of the target is
    # built after it exists (CMake orders a target's objects after its
    # generated sources).
    target_sources(${target} PRIVATE "${c_file}" "${h_file}")
    target_include_directories(${target} PRIVATE "${out_dir}")
endfunction()

# serval_add_soundbank(<target> <name> <files...>)
#
# Builds a Maxmod sound bank for a target made by serval_add_rom() (or any
# target) at build time, from modules (.mod, .s3m, .xm, .it) and WAV samples
# (.wav), paths relative to the current source directory: BlocksDS's mmutil
# builds the bank, and tools/soundbank.py turns it into <name>.c, which
# defines `const unsigned char <name>[]` (in ROM) and joins the target's
# sources, and <name>.h, mmutil's numbers: MOD_<FILE> for each module and
# SFX_<FILE> for each sample, from 0 in the order the files are given (a
# module's samples are numbered with the samples), and MSL_NSONGS,
# MSL_NSAMPS and MSL_BANKSIZE. Both go in the target's binary directory, on
# its include path, so the game includes <name>.h and calls
# audio_bank_set(<name>); scripts get the same names from the header
# (serval_add_script(... HEADERS ${CMAKE_CURRENT_BINARY_DIR}/<name>.h)).
#
# The build refuses a file of another kind, two files whose names give the
# same define (mmutil upper-cases a name up to its first dot and turns
# punctuation into underscores), and a sample named "none" (its SFX_NONE would
# clash with audio.h's). The mmutil must be the version serval.json's
# toolchain.mmutil names (the bank format of the engine's Maxmod): found as
# SERVAL_MMUTIL (a CMake or environment variable, which tools/setup-dev.sh
# sets) or as mmutil on the PATH; tools/build-mmutil.sh builds it. The bank is
# rebuilt when a file, mmutil or soundbank.py changes. Web builds build the
# bank too (the game's code names it), though they don't play it
# (docs/audio.md#web). Call it from the directory that defined the target,
# after serval_add_rom().
function(serval_add_soundbank target name)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "serval_add_soundbank(${target}): no such target; call it after "
                            "serval_add_rom(${target} ...).")
    endif()
    if(NOT name MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
        message(FATAL_ERROR "serval_add_soundbank(${target}): the bank's name \"${name}\" is "
                            "not a C identifier.")
    endif()
    if(ARGC LESS 3)
        message(FATAL_ERROR "serval_add_soundbank(${target} ${name}): no modules or samples.")
    endif()
    set(engine_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
    set(tool "${engine_dir}/tools/soundbank.py")
    if(NOT EXISTS "${tool}")
        message(FATAL_ERROR "serval_add_soundbank(${target}): ${tool} is missing.")
    endif()
    if(NOT SERVAL_PYTHON_EXECUTABLE)
        message(FATAL_ERROR "serval_add_soundbank(${target}): Python 3 is needed to build the "
                            "bank (SERVAL_PYTHON_EXECUTABLE is empty).")
    endif()
    file(READ "${engine_dir}/serval.json" manifest)
    string(JSON version ERROR_VARIABLE no_version GET "${manifest}" toolchain mmutil)
    if(no_version)
        message(FATAL_ERROR "serval_add_soundbank(${target}): serval.json names no "
                            "toolchain.mmutil version.")
    endif()
    _serval_find_mmutil(mmutil "${version}" "${target}")

    set(files "")
    foreach(file IN LISTS ARGN)
        cmake_path(ABSOLUTE_PATH file BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
        # A file another custom command generates exists only once built.
        get_source_file_property(generated "${file}" GENERATED)
        if(NOT EXISTS "${file}" AND NOT generated)
            message(FATAL_ERROR "serval_add_soundbank(${target}): ${file} does not exist.")
        endif()
        list(APPEND files "${file}")
    endforeach()
    get_target_property(out_dir ${target} BINARY_DIR)
    set(c_file "${out_dir}/${name}.c")
    set(h_file "${out_dir}/${name}.h")
    add_custom_command(
        OUTPUT "${c_file}" "${h_file}"
        COMMAND "${SERVAL_PYTHON_EXECUTABLE}" "${tool}" --mmutil "${mmutil}" --version "${version}"
                --name "${name}" --out-dir "${out_dir}" ${files}
        DEPENDS ${files} "${tool}" "${mmutil}"
        COMMENT "Building the sound bank ${name}"
        VERBATIM)
    # The header is a source too, so every object of the target is built
    # after it exists.
    target_sources(${target} PRIVATE "${c_file}" "${h_file}")
    target_include_directories(${target} PRIVATE "${out_dir}")
endfunction()

# _serval_find_mmutil(<out_var> <version> <target>)
#
# Sets <out_var> to the mmutil to run: SERVAL_MMUTIL (a cache or environment
# variable) or mmutil on the PATH, checked once to say "mmutil v<version>".
function(_serval_find_mmutil out_var version target)
    if(NOT SERVAL_MMUTIL AND DEFINED ENV{SERVAL_MMUTIL})
        set(SERVAL_MMUTIL "$ENV{SERVAL_MMUTIL}" CACHE FILEPATH
            "BlocksDS's mmutil, which builds sound banks (serval_add_soundbank)")
    endif()
    if(NOT SERVAL_MMUTIL)
        find_program(SERVAL_MMUTIL mmutil
            DOC "BlocksDS's mmutil, which builds sound banks (serval_add_soundbank)")
    endif()
    if(NOT SERVAL_MMUTIL OR NOT EXISTS "${SERVAL_MMUTIL}")
        message(FATAL_ERROR "serval_add_soundbank(${target}): mmutil ${version} (BlocksDS's, "
                            "which builds sound banks) was not found. Install it with "
                            "tools/setup-dev.sh, or tools/build-mmutil.sh DIR, and point "
                            "SERVAL_MMUTIL (a CMake or environment variable) at DIR/mmutil.")
    endif()
    if(NOT SERVAL_MMUTIL_CHECKED STREQUAL "${SERVAL_MMUTIL}|${version}")
        execute_process(COMMAND "${SERVAL_MMUTIL}" -V OUTPUT_VARIABLE says
                        OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE result)
        if(NOT says STREQUAL "mmutil v${version}")
            message(FATAL_ERROR "serval_add_soundbank(${target}): ${SERVAL_MMUTIL} says "
                                "\"${says}\", not \"mmutil v${version}\": the engine's Maxmod "
                                "reads the banks of mmutil ${version} (serval.json). Install it "
                                "with tools/build-mmutil.sh and point SERVAL_MMUTIL at it.")
        endif()
        set(SERVAL_MMUTIL_CHECKED "${SERVAL_MMUTIL}|${version}" CACHE INTERNAL
            "The mmutil and version serval_add_soundbank() checked")
    endif()
    set(${out_var} "${SERVAL_MMUTIL}" PARENT_SCOPE)
endfunction()

include("${CMAKE_CURRENT_LIST_DIR}/ServalWeb.cmake")
