# Coverage.cmake — gcov-format instrumentation + `coverage` report target.
#
# Both GCC and Clang emit gcov-compatible data with `--coverage`, so one report tool (gcovr)
# handles both; for Clang, gcovr is told to use `llvm-cov gcov`. gcovr is pinned in uv.lock and
# run through `uv run` when uv is installed; a gcovr already on PATH is used otherwise.
# Gate: FASTBUS_COVERAGE_FAIL_UNDER (line %). 0 in M0 (report only), 85 from M1.

set(FASTBUS_COVERAGE_FAIL_UNDER "0" CACHE STRING "Minimum line coverage (%) for the gate")

add_library(fastbus_coverage INTERFACE)
add_library(fastbus::coverage ALIAS fastbus_coverage)

if(FASTBUS_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" OR MSVC)
        message(FATAL_ERROR "FASTBUS_COVERAGE requires GCC or Clang")
    endif()
    # -fprofile-update=atomic: gcov counters are plain increments by default, so code run by two
    # threads at once (ring tests) corrupts them into negative hit counts that gcovr rejects.
    target_compile_options(fastbus_coverage INTERFACE --coverage -fprofile-update=atomic -O0 -g)
    target_link_options(fastbus_coverage INTERFACE --coverage)

    find_program(FASTBUS_UV uv)
    find_program(FASTBUS_GCOVR gcovr)
    if(FASTBUS_UV)
        set(_gcovr_cmd "${FASTBUS_UV}" run --frozen --project "${PROJECT_SOURCE_DIR}" gcovr)
    elseif(FASTBUS_GCOVR)
        set(_gcovr_cmd "${FASTBUS_GCOVR}")
    endif()
    if(_gcovr_cmd)
        set(_gcov_exe "gcov")
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            set(_gcov_exe "llvm-cov gcov")
        endif()
        set(_report_dir "${PROJECT_BINARY_DIR}/coverage")
        add_custom_target(coverage
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_report_dir}"
            COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${PROJECT_BINARY_DIR}" --output-on-failure
            COMMAND ${_gcovr_cmd}
                --root "${PROJECT_SOURCE_DIR}"
                --object-directory "${PROJECT_BINARY_DIR}"
                --gcov-executable "${_gcov_exe}"
                --filter "${PROJECT_SOURCE_DIR}/include/"
                --filter "${PROJECT_SOURCE_DIR}/src/"
                --exclude-unreachable-branches
                --print-summary
                --xml "${_report_dir}/coverage.xml"
                --html-details "${_report_dir}/index.html"
                --fail-under-line "${FASTBUS_COVERAGE_FAIL_UNDER}"
            WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
            COMMENT "Running tests and generating coverage report in ${_report_dir}"
            VERBATIM)
    else()
        message(WARNING "FASTBUS_COVERAGE is ON but neither uv nor gcovr was found; "
                        "install uv (https://docs.astral.sh/uv/) to get the `coverage` target")
    endif()
endif()
