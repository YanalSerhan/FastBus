# Test tree. Labels let presets/CI select subsets:
#   unit, integration, stress, chaos, golden, canary
add_subdirectory(unit)
add_subdirectory(stress)

# Canaries prove each sanitizer is really active: they contain a deliberate bug and are
# expected to FAIL (WILL_FAIL). Built only in the matching sanitizer configuration.
if(FASTBUS_ACTIVE_SANITIZERS)
    add_subdirectory(canary)
endif()
