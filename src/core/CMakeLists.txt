# fastbus::vocabulary — Result/Error/fail_fast: dependency-free, below platform (ADR-002).
# Files stay in core (same paths, same namespace); only the link graph places them lower, so
# platform can report errors with the same vocabulary without depending on core.
fastbus_add_library(vocabulary
    SOURCES
        assert.cpp
        error.cpp)

# fastbus::core — strong types, time, SPSC/MPSC rings, CRC32C.
# Most of core is header-only templates; the .cpp files hold the few non-template pieces.
fastbus_add_library(core
    SOURCES
        build_info.cpp
        sleep.cpp
    PUBLIC_DEPS
        fastbus::platform
        fastbus::vocabulary)
