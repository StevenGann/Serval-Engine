# Web builds (Emscripten): the engine's GBA backend compiled to WebAssembly and
# run on virtual GBA hardware (src/web/). Included by Serval.cmake.

# _serval_web_font(<output.c>)
#
# Converts libtonc's sys8 font glyphs (assembly data, which Emscripten can't
# assemble) to a C array.
function(_serval_web_font output)
    set(engine_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
    set(source "${engine_dir}/third_party/libtonc/src/font/sys8.s")
    file(STRINGS "${source}" lines)
    set(in_glyphs FALSE)
    set(words "")
    foreach(line IN LISTS lines)
        if(line MATCHES "^sys8Glyphs:")
            set(in_glyphs TRUE)
        elseif(in_glyphs AND line MATCHES "^[ \t]*\\.word[ \t]+(.*)$")
            string(APPEND words "    ${CMAKE_MATCH_1},\n")
        elseif(in_glyphs AND line MATCHES "^@}}BLOCK")
            break()
        endif()
    endforeach()
    file(CONFIGURE OUTPUT "${output}" CONTENT
         "// Generated from libtonc's src/font/sys8.s (cmake/ServalWeb.cmake).\n\nextern const unsigned int sys8Glyphs[192];\nconst unsigned int sys8Glyphs[192] = {\n${words}};\n")
endfunction()

# _serval_web_escape(<out_var> HTML|JSON <text>)
#
# <text> written for the page: letters and digits as they are, and every
# other character as an HTML character reference (&#60; for <; spaces stay as
# they are) or a JSON string escape (\u003c for <, \u0020 for a space). The
# result is only letters, digits, spaces and escapes, so no printable ASCII
# text can end the <title> or <script> element, start a character reference
# of its own, end the JSON string, or be taken for something of theirs by
# configure_file() or by Emscripten's processing of the page (its
# preprocessor's # lines and {{{ }}} placeholders).
function(_serval_web_escape out_var kind text)
    set(escaped "")
    string(LENGTH "${text}" length)
    if(length GREATER 0)
        math(EXPR last "${length} - 1")
        foreach(i RANGE ${last})
            string(SUBSTRING "${text}" ${i} 1 c)
            if(c MATCHES "^[A-Za-z0-9]$" OR (kind STREQUAL "HTML" AND c STREQUAL " "))
                string(APPEND escaped "${c}")
            else()
                string(HEX "${c}" hex)
                if(kind STREQUAL "HTML")
                    math(EXPR code "0x${hex}")
                    string(APPEND escaped "&#${code};")
                else()
                    string(APPEND escaped "\\u00${hex}")
                endif()
            endif()
        endforeach()
    endif()
    set(${out_var} "${escaped}" PARENT_SCOPE)
endfunction()

# _serval_add_web_page(<target> <title> <game_code> <sources...>)
#
# Builds <target>.html: the game and the engine as one self-contained page
# (WebAssembly embedded), from the src/web/shell.html template. The title is
# the page's title, and the title and game code name the game's saves in
# localStorage; both are printable ASCII (serval_add_rom() checks), and reach
# the page escaped (_serval_web_escape), so any such character works. The
# sources include the game's save type's object (serval_save_<type>: save
# memory of that size).
function(_serval_add_web_page target title game_code)
    set(engine_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
    set(shell "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${target}-shell.html")
    # <title>, and the JSON the page's script reads both from.
    _serval_web_escape(SERVAL_WEB_TITLE HTML "${title}")
    _serval_web_escape(title_json JSON "${title}")
    _serval_web_escape(game_code_json JSON "${game_code}")
    set(SERVAL_WEB_ROM "{\"title\":\"${title_json}\",\"gameCode\":\"${game_code_json}\"}")
    configure_file("${engine_dir}/src/web/shell.html" "${shell}" @ONLY)

    # Wasm memory reaches past the GBA's OAM (0x07000000-0x070003FF), where
    # the GBA code finds the hardware it programs. Most of it is never touched,
    # and browsers only back the pages that are. (Set here, not in the file's
    # scope: games call this from their own directories.)
    set(memory 117506048) # 0x07010000

    add_executable(${target} ${ARGN})
    set_target_properties(${target} PROPERTIES SUFFIX ".html")
    target_link_libraries(${target} PRIVATE serval)
    target_link_options(${target} PRIVATE
        "--shell-file=${shell}"
        -sSINGLE_FILE=1
        # The page exactly as written, escapes included. Emscripten's HTML
        # minifier decodes character references and escapes the text again,
        # not always right: the title "&#60", written &#38;&#35;60, comes out
        # as &#60, which the browser shows as "<". It would save 3 to 10 KB.
        -sMINIFY_HTML=0
        -sENVIRONMENT=web
        # frame_end() waits for VBlank inside the game's loop; Asyncify lets
        # that wait hand control back to the browser and resume afterwards.
        -sASYNCIFY=1
        -sASYNCIFY_STACK_SIZE=16384
        -sINVOKE_RUN=0
        "-sEXPORTED_RUNTIME_METHODS=callMain,HEAPU8,HEAPU16,HEAPF32"
        -sINITIAL_MEMORY=${memory}
        -sALLOW_MEMORY_GROWTH=0
        -sFILESYSTEM=0
        -sMALLOC=emmalloc)
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${shell}")
endfunction()
