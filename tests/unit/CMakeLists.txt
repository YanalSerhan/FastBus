fastbus_add_test(fastbus_unit_version
    SOURCES version_test.cpp
    DEPS fastbus::core
    LABELS unit)

fastbus_add_test(fastbus_unit_core_types
    SOURCES
        strong_type_test.cpp
        time_test.cpp
        result_test.cpp
    DEPS fastbus::core
    LABELS unit)
