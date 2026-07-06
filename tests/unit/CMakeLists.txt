add_pureretro_test(test_smoke test_smoke.c)
add_pureretro_test(test_vfs test_vfs.c ${CMAKE_SOURCE_DIR}/src/io/vfs.c)
add_pureretro_test(test_core_content test_core_content.c
    ${CMAKE_SOURCE_DIR}/src/core/core_content.c)
add_pureretro_test(test_input test_input.c
    ${CMAKE_SOURCE_DIR}/src/input/input.c
    ${CMAKE_SOURCE_DIR}/src/logging/log.c)
add_pureretro_test(test_log test_log.c
    ${CMAKE_SOURCE_DIR}/src/logging/log.c)
add_pureretro_test(test_core_variables_parse test_core_variables_parse.c
    ${CMAKE_SOURCE_DIR}/src/core/core_variables_parse.c)
add_pureretro_test(test_core_variables test_core_variables.c
    ${CMAKE_SOURCE_DIR}/src/core/core_variables.c
    ${CMAKE_SOURCE_DIR}/src/core/core_variables_parse.c
    ${CMAKE_SOURCE_DIR}/src/logging/log.c)
add_pureretro_test(test_cli test_cli.c
    ${CMAKE_SOURCE_DIR}/src/cli/cli.c
    ${CMAKE_SOURCE_DIR}/src/core/core_variables.c
    ${CMAKE_SOURCE_DIR}/src/core/core_variables_parse.c
    ${CMAKE_SOURCE_DIR}/src/logging/log.c)
