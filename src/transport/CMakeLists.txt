# fastbus::transport — shared-memory segments and loans (M2), UDP + reliability (M6).
fastbus_add_library(transport
    SOURCES
        segment.cpp
        segment_validate.cpp
    PUBLIC_DEPS
        fastbus::core
        fastbus::platform)
