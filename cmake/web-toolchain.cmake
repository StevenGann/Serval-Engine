# CMake toolchain file for web builds: WebAssembly through Emscripten.
#
# Emscripten is found through $EMSDK (set by emsdk_env.sh), or on PATH.

if(DEFINED ENV{EMSDK} AND EXISTS "$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
    include("$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
else()
    find_program(_serval_emcc emcc)
    if(NOT _serval_emcc)
        message(FATAL_ERROR "Emscripten not found: install emsdk and source emsdk_env.sh "
                            "(docs/development.md).")
    endif()
    cmake_path(GET _serval_emcc PARENT_PATH _serval_emscripten_dir)
    include("${_serval_emscripten_dir}/cmake/Modules/Platform/Emscripten.cmake")
endif()
