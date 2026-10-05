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

# _serval_add_web_page(<target> <title> <sources...>)
#
# Builds <target>.html: the game and the engine as one self-contained page
# (WebAssembly embedded), from the src/web/shell.html template.
function(_serval_add_web_page target title)
    set(engine_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
    set(shell "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${target}-shell.html")
    set(SERVAL_WEB_TITLE "${title}")
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
