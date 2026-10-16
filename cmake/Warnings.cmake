# Warnings.cmake — strict warning flags as an INTERFACE target.
#
# `fastbus::warnings` is linked PRIVATE into fastbus targets only (see FastbusTargets.cmake).
# It is never applied globally, so third-party code pulled in by FetchContent is not compiled
# with our flags and cannot break the build with its own warnings.
#
# Three compiler front-ends are handled:
#   * MSVC cl.exe            /W4 family
#   * clang-cl (Windows)     MSVC-style driver flags + the same -W extras as GCC/Clang
#   * GCC / Clang            -Wall family
# clang-cl rejects /external:anglebrackets (unused-argument error under /WX), so the
# /external:* switches are cl.exe-only. CMake already passes SYSTEM include dirs to clang-cl
# via -imsvc, which silences third-party headers there.
#
# Intentionally disabled warnings: none so far. Any future exception must be listed here
# with a reason (and mirrored in docs/design-decisions.md).

add_library(fastbus_warnings INTERFACE)
add_library(fastbus::warnings ALIAS fastbus_warnings)

set(_fastbus_gnu_extra_warnings
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wold-style-cast
    -Wnon-virtual-dtor
    -Woverloaded-virtual
    -Wcast-align
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2)

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(fastbus_warnings INTERFACE
        /W4
        /permissive-
        /Zc:__cplusplus
        /utf-8
        /external:W0           # no warnings from SYSTEM / external headers
        /external:anglebrackets)
    if(FASTBUS_WARNINGS_AS_ERRORS)
        target_compile_options(fastbus_warnings INTERFACE /WX)
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    # clang-cl: /W4 maps to -Wall -Wextra; add -Wpedantic and the GCC/Clang extras.
    target_compile_options(fastbus_warnings INTERFACE
        /W4
        /permissive-
        /Zc:__cplusplus
        /utf-8
        -Wpedantic
        ${_fastbus_gnu_extra_warnings})
    if(FASTBUS_WARNINGS_AS_ERRORS)
        target_compile_options(fastbus_warnings INTERFACE /WX)
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(fastbus_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        ${_fastbus_gnu_extra_warnings})
    if(FASTBUS_WARNINGS_AS_ERRORS)
        target_compile_options(fastbus_warnings INTERFACE -Werror)
    endif()
else()
    message(WARNING "fastbus: unknown compiler '${CMAKE_CXX_COMPILER_ID}', no warning flags set")
endif()
