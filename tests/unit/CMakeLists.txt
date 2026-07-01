add_pureretro_test(test_smoke test_smoke.c)
add_pureretro_test(test_vfs   test_vfs.c   ${CMAKE_SOURCE_DIR}/src/vfs.c)
add_pureretro_test(test_core_content test_core_content.c ${CMAKE_SOURCE_DIR}/src/core_content.c)
add_pureretro_test(test_input test_input.c ${CMAKE_SOURCE_DIR}/src/input.c ${CMAKE_SOURCE_DIR}/src/log.c)
