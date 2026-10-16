# Warnings.cmake — strict warning flags as an INTERFACE target.
#
# `fastbus::warnings` is linked PRIVATE into fastbus targets only (see FastbusTargets.cmake).
# It is never applied globally, so third-party code pulled in by FetchContent is not compiled
# with our flags and cannot break the build with its own warnings.
#
# Intentionally disabled warnings: none so far. Any future exception must be listed here
# with a reason (and mirrored in docs/design-decisions.md).

add_library(fastbus_warnings INTERFACE)
add_library(fastbus::warnings ALIAS fastbus_warnings)

if(MSVC)
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
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(fastbus_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
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
    if(FASTBUS_WARNINGS_AS_ERRORS)
        target_compile_options(fastbus_warnings INTERFACE -Werror)
    endif()
else()
    message(WARNING "fastbus: unknown compiler '${CMAKE_CXX_COMPILER_ID}', no warning flags set")
endif()
