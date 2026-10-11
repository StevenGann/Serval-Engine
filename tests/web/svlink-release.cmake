# debug link: builds the fireflies example as a release page (no agent), for
# CTest svlink_e2e's check that a release page never connects
# (tools/svlink_test.py): a build of its own, in BUILD, from the engine in
# SOURCE, with the web toolchain and CMAKE_BUILD_TYPE Release, as the
# web-release preset builds it.
#
#   cmake -DSOURCE=<engine> -DBUILD=<dir> -DGENERATOR=<generator> -P svlink-release.cmake
foreach(var IN ITEMS SOURCE BUILD GENERATOR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "svlink-release.cmake: -D${var}=... is missing")
    endif()
endforeach()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${SOURCE}" -B "${BUILD}" -G "${GENERATOR}"
            "-DCMAKE_TOOLCHAIN_FILE=${SOURCE}/cmake/web-toolchain.cmake"
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
