# Shared helper for declaring a Unity test
function(add_pureretro_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(${name} PRIVATE unity SDL3::SDL3)
    target_compile_definitions(${name} PRIVATE PURERETRO_TESTING)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_subdirectory(unit)
