# fastbus::platform — OS abstraction (clocks, threads, shared memory, processes, sockets).
#
# Sources come from exactly one backend directory chosen in cmake/Platform.cmake (ADR-002):
#   windows/  or  posix/
# Each backend implements the same headers in include/fastbus/platform/.
fastbus_add_library(platform
    SOURCES
        ${FASTBUS_PLATFORM_SOURCE_DIR}/time_source.cpp
    PUBLIC_DEPS
        fastbus::platform_libs)
