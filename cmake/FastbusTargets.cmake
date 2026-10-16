# FastbusTargets.cmake — the only way first-party targets are created.
#
#   fastbus_add_library(<name> [STATIC|INTERFACE] SOURCES ... [PUBLIC_DEPS ...] [PRIVATE_DEPS ...])
#       -> target fastbus_<name>, alias fastbus::<name>
#   fastbus_add_executable(<name> SOURCES ... [DEPS ...])
#   fastbus_add_test(<name> SOURCES ... [DEPS ...] [LABELS ...] [TIMEOUT s])
#       -> GoogleTest executable registered with ctest via gtest_discover_tests
#   fastbus_add_benchmark(<name> SOURCES ... [DEPS ...])
#
# Every target gets: C++20, warnings (PRIVATE), coverage instrumentation when enabled,
# clang-tidy when enabled, and include paths for include/ + generated headers.

function(_fastbus_common target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_link_libraries(${target} PRIVATE fastbus::warnings fastbus::coverage)
    fastbus_enable_tidy(${target})
endfunction()

function(fastbus_add_library name)
    cmake_parse_arguments(ARG "STATIC;INTERFACE" "" "SOURCES;PUBLIC_DEPS;PRIVATE_DEPS" ${ARGN})
    set(target fastbus_${name})
    if(ARG_INTERFACE)
        add_library(${target} INTERFACE)
        target_include_directories(${target} INTERFACE
            "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
            "$<BUILD_INTERFACE:${FASTBUS_GENERATED_INCLUDE_DIR}>")
        target_compile_features(${target} INTERFACE cxx_std_20)
        target_link_libraries(${target} INTERFACE ${ARG_PUBLIC_DEPS})
    else()
        add_library(${target} STATIC ${ARG_SOURCES})
        target_include_directories(${target}
            PUBLIC
                "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
                "$<BUILD_INTERFACE:${FASTBUS_GENERATED_INCLUDE_DIR}>"
            PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(${target} PUBLIC ${ARG_PUBLIC_DEPS} PRIVATE ${ARG_PRIVATE_DEPS})
        _fastbus_common(${target})
    endif()
    add_library(fastbus::${name} ALIAS ${target})
endfunction()

function(fastbus_add_executable name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE ${ARG_DEPS})
    _fastbus_common(${name})
endfunction()

function(fastbus_add_test name)
    cmake_parse_arguments(ARG "" "TIMEOUT" "SOURCES;DEPS;LABELS" ${ARGN})
    if(NOT ARG_TIMEOUT)
        set(ARG_TIMEOUT 60)
    endif()
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE ${ARG_DEPS} GTest::gtest GTest::gmock GTest::gtest_main)
    _fastbus_common(${name})
    # PRE_TEST discovery: listing tests at ctest time avoids running sanitized binaries at
    # build time and works for cross-compiles.
    gtest_discover_tests(${name}
        DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS "${ARG_LABELS}" TIMEOUT ${ARG_TIMEOUT})
endfunction()

function(fastbus_add_benchmark name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE ${ARG_DEPS} benchmark::benchmark benchmark::benchmark_main)
    _fastbus_common(${name})
endfunction()

function(fastbus_print_summary)
    message(STATUS "")
    message(STATUS "fastbus ${PROJECT_VERSION} configuration")
    message(STATUS "  compiler         : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
    message(STATUS "  build type       : ${CMAKE_BUILD_TYPE}${CMAKE_CONFIGURATION_TYPES}")
    message(STATUS "  platform backend : ${FASTBUS_PLATFORM}")
    message(STATUS "  sanitizers       : ${FASTBUS_ACTIVE_SANITIZERS}")
    message(STATUS "  warnings as error: ${FASTBUS_WARNINGS_AS_ERRORS}")
    message(STATUS "  coverage / tidy  : ${FASTBUS_COVERAGE} / ${FASTBUS_ENABLE_TIDY}")
    message(STATUS "  tests/bench/ex/tools: ${FASTBUS_BUILD_TESTS}/${FASTBUS_BUILD_BENCHMARKS}/"
                   "${FASTBUS_BUILD_EXAMPLES}/${FASTBUS_BUILD_TOOLS}")
    message(STATUS "")
endfunction()
