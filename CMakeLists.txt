# fastbus — root build script.
#
# Layout and rules are described in docs/PLAN.md and CONTRIBUTING.md.
# Every first-party target is created through the helpers in cmake/FastbusTargets.cmake
# so warnings, include paths, aliases and test registration are applied consistently.

cmake_minimum_required(VERSION 3.24)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are not allowed. Use a preset, e.g. `cmake --preset linux-gcc`, "
        "and delete the CMakeCache.txt / CMakeFiles that were just created in the source tree.")
endif()

project(fastbus
    VERSION 0.1.0
    DESCRIPTION "Low-latency shared-memory / UDP publish-subscribe middleware"
    HOMEPAGE_URL "https://github.com/YanalSerhan/FastBus"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------------------------
option(FASTBUS_BUILD_TESTS "Build unit/integration tests" ${PROJECT_IS_TOP_LEVEL})
option(FASTBUS_BUILD_BENCHMARKS "Build Google Benchmark suites" ${PROJECT_IS_TOP_LEVEL})
option(FASTBUS_BUILD_EXAMPLES "Build examples" ${PROJECT_IS_TOP_LEVEL})
option(FASTBUS_BUILD_TOOLS "Build command-line tools (fbgen, fbtop, ...)" ${PROJECT_IS_TOP_LEVEL})
option(FASTBUS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors on fastbus targets" ON)
option(FASTBUS_COVERAGE "Instrument fastbus targets for coverage (GCC/Clang)" OFF)
option(FASTBUS_ENABLE_TIDY "Run clang-tidy on fastbus targets during the build" OFF)
set(FASTBUS_SANITIZER "none" CACHE STRING
    "Sanitizer(s): none | address | undefined | thread | address,undefined")
set_property(CACHE FASTBUS_SANITIZER PROPERTY STRINGS
    none address undefined thread "address,undefined")

# ---------------------------------------------------------------------------------------------
# Build modules
# ---------------------------------------------------------------------------------------------
include(Warnings)
include(Sanitizers)
include(Platform)
include(Coverage)
include(Tidy)
include(Format)
include(Dependencies)
include(FastbusTargets)

# Generated headers (version.hpp) live in the build tree, never in the source tree.
set(FASTBUS_GENERATED_INCLUDE_DIR "${PROJECT_BINARY_DIR}/generated/include")
configure_file(
    "${PROJECT_SOURCE_DIR}/include/fastbus/version.hpp.in"
    "${FASTBUS_GENERATED_INCLUDE_DIR}/fastbus/version.hpp"
    @ONLY)

# ---------------------------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------------------------
add_subdirectory(src)

if(FASTBUS_BUILD_TESTS)
    enable_testing()
    fastbus_fetch_googletest()
    add_subdirectory(tests)
endif()

if(FASTBUS_BUILD_BENCHMARKS)
    fastbus_fetch_benchmark()
    add_subdirectory(benchmarks)
endif()

if(FASTBUS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(FASTBUS_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

add_subdirectory(modules/ekf)

fastbus_print_summary()
