# debug link: builds the fireflies example for release (no agent), for the
# end-to-end tests' check that a release build never connects
# (tools/svlink_test.py): a build of its own, in BUILD, from the engine in
# SOURCE, with CMAKE_BUILD_TYPE Release and the web toolchain, as the
# web-release preset builds it (CTest svlink_e2e), or with TOOLCHAIN (the
# GBA's, arm-gba-toolchain.cmake, as gba-release: CTest svlink_emu_e2e).
# MMUTIL, when given, is passed on as SERVAL_MMUTIL (the examples' sound
# banks).
#
#   cmake -DSOURCE=<engine> -DBUILD=<dir> -DGENERATOR=<generator> [-DTOOLCHAIN=<file>]
#         [-DMMUTIL=<mmutil>] -P svlink-release.cmake
foreach(var IN ITEMS SOURCE BUILD GENERATOR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "svlink-release.cmake: -D${var}=... is missing")
    endif()
endforeach()
if(NOT DEFINED TOOLCHAIN)
    set(TOOLCHAIN "${SOURCE}/cmake/web-toolchain.cmake")
endif()
set(extra)
if(MMUTIL)
    list(APPEND extra "-DSERVAL_MMUTIL=${MMUTIL}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${SOURCE}" -B "${BUILD}" -G "${GENERATOR}"
            "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN}" ${extra}
            -DCMAKE_BUILD_TYPE=Release -DSERVAL_BUILD_TESTS=OFF -DSERVAL_BUILD_EXAMPLES=ON
    OUTPUT_QUIET RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "svlink-release.cmake: configuring the release build in ${BUILD} failed")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${BUILD}" --target fireflies
                OUTPUT_QUIET RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "svlink-release.cmake: building fireflies in ${BUILD} failed")
endif()
