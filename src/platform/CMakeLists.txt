# fastbus::platform — OS abstraction (clocks, threads, shared memory, processes, sockets).
#
# Sources come from exactly one backend directory chosen in cmake/Platform.cmake (ADR-002):
#   windows/  or  posix/
# Each backend implements the same headers in include/fastbus/platform/. Code that is the same
# for both backends lives in common/. Errors use the dependency-free vocabulary (ADR-002).
fastbus_add_library(platform
    SOURCES
        common/process_common.cpp
        common/shared_memory_common.cpp
        ${FASTBUS_PLATFORM_SOURCE_DIR}/process.cpp
        ${FASTBUS_PLATFORM_SOURCE_DIR}/shared_memory.cpp
        ${FASTBUS_PLATFORM_SOURCE_DIR}/thread.cpp
        ${FASTBUS_PLATFORM_SOURCE_DIR}/time_source.cpp
    PUBLIC_DEPS
        fastbus::platform_libs
        fastbus::vocabulary)
