# Library modules, lowest layer first (see docs/PLAN.md for the dependency graph).
# core is added first only so that fastbus::vocabulary (defined there, used by platform) exists;
# the dependency direction is still platform -> vocabulary, core -> platform (ADR-002).
add_subdirectory(core)
add_subdirectory(platform)
add_subdirectory(transport)
add_subdirectory(faults)
add_subdirectory(record)
add_subdirectory(idl)
add_subdirectory(api)
