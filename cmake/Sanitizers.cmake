# Sanitizers.cmake — validates FASTBUS_SANITIZER and applies sanitizer flags.
#
# Sanitizer flags are applied GLOBALLY (add_compile_options / add_link_options), unlike
# warnings: TSan in particular reports false positives if some code (e.g. GoogleTest) is not
# instrumented, and ASan needs every TU that allocates to agree on the runtime.
#
# Accepted values: none | address | undefined | thread | address,undefined
# Refused: thread combined with address (the runtimes are mutually exclusive).

string(REPLACE "," ";" _fastbus_sanitizers "${FASTBUS_SANITIZER}")
list(TRANSFORM _fastbus_sanitizers STRIP)
list(TRANSFORM _fastbus_sanitizers TOLOWER)
list(REMOVE_DUPLICATES _fastbus_sanitizers)
set(FASTBUS_ACTIVE_SANITIZERS "")

foreach(_san IN LISTS _fastbus_sanitizers)
    if(_san STREQUAL "none" OR _san STREQUAL "")
        continue()
    endif()
    if(NOT _san MATCHES "^(address|undefined|thread)$")
        message(FATAL_ERROR "FASTBUS_SANITIZER: unknown sanitizer '${_san}'")
    endif()
    list(APPEND FASTBUS_ACTIVE_SANITIZERS "${_san}")
endforeach()

if("thread" IN_LIST FASTBUS_ACTIVE_SANITIZERS AND "address" IN_LIST FASTBUS_ACTIVE_SANITIZERS)
    message(FATAL_ERROR "FASTBUS_SANITIZER: 'thread' cannot be combined with 'address'")
endif()

if(FASTBUS_ACTIVE_SANITIZERS)
    if(MSVC)
        if(NOT FASTBUS_ACTIVE_SANITIZERS STREQUAL "address")
            message(FATAL_ERROR "FASTBUS_SANITIZER: MSVC supports only 'address'")
        endif()
        add_compile_options(/fsanitize=address /Zi)
        # MSVC ASan is incompatible with incremental linking and /RTC.
        add_link_options(/INCREMENTAL:NO)
        string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        list(JOIN FASTBUS_ACTIVE_SANITIZERS "," _san_list)
        set(_san_flags "-fsanitize=${_san_list}" -fno-omit-frame-pointer -g)
        if("undefined" IN_LIST FASTBUS_ACTIVE_SANITIZERS)
            list(APPEND _san_flags -fno-sanitize-recover=all)
        endif()
        add_compile_options(${_san_flags})
        add_link_options("-fsanitize=${_san_list}")
    else()
        message(FATAL_ERROR "FASTBUS_SANITIZER: unsupported compiler '${CMAKE_CXX_COMPILER_ID}'")
    endif()
endif()

# Lets tests/canary pick the canaries matching the active sanitizer(s).
set(FASTBUS_ACTIVE_SANITIZERS "${FASTBUS_ACTIVE_SANITIZERS}" CACHE INTERNAL "")
