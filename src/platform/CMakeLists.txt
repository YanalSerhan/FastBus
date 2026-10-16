# fastbus::platform — OS abstraction (clocks, threads, shared memory, processes, sockets).
#
# Sources come from exactly one backend directory chosen in cmake/Platform.cmake (ADR-002):
#   windows/  or  posix/
# Until M1 adds real sources this is an INTERFACE target carrying include paths + system libs.
fastbus_add_library(platform INTERFACE
    PUBLIC_DEPS
        fastbus::platform_libs)
