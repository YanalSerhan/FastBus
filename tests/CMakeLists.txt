# Test tree. Labels let presets/CI select subsets:
#   unit, integration, stress, chaos, golden, canary, alloc
add_subdirectory(helpers)  # first: defines fastbus_use_test_child()
add_subdirectory(unit)
add_subdirectory(stress)
# Allocation tests replace the global operator new/delete. TSan's runtime defines them too
# (duplicate symbols at link time), and MSVC's ASan runtime hooks them, so these tests run in
# the plain and Linux ASan/UBSan builds only (KI-009).
if(NOT "thread" IN_LIST FASTBUS_ACTIVE_SANITIZERS AND NOT (MSVC AND FASTBUS_ACTIVE_SANITIZERS))
    add_subdirectory(alloc)
endif()

# Canaries prove each sanitizer is really active: they contain a deliberate bug and are
# expected to FAIL (WILL_FAIL). Built only in the matching sanitizer configuration.
if(FASTBUS_ACTIVE_SANITIZERS)
    add_subdirectory(canary)
endif()
