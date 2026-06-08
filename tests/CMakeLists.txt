# Shared helper for declaring a Unity test.
#
# Test targets are EXCLUDE_FROM_ALL: a default `cmake --build` only links
# `pureretro`. Each test is paired with a build-on-demand fixture that runs
# `cmake --build . --target <name>` before the test executes, so a single
# `ctest` invocation builds and runs the tests.
function(add_pureretro_test name)
    add_executable(${name} EXCLUDE_FROM_ALL ${ARGN})
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(${name} PRIVATE unity SDL3::SDL3)
    target_compile_definitions(${name} PRIVATE PURERETRO_TESTING)

    set(_build_fixture build_${name})
    add_test(NAME ${_build_fixture}
        COMMAND "${CMAKE_COMMAND}"
                --build "${CMAKE_BINARY_DIR}"
                --config "$<CONFIG>"
                --target ${name}
    )
    set_tests_properties(${_build_fixture} PROPERTIES
        FIXTURES_SETUP ${_build_fixture}
    )

    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        FIXTURES_REQUIRED ${_build_fixture}
    )
endfunction()

add_subdirectory(unit)
