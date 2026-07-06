cmake_minimum_required(VERSION 3.16)
project(PureRetro
    VERSION 0.1.0
    DESCRIPTION "A minimal libretro frontend in Pure C + SDL3"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Options
option(PURERETRO_ENABLE_VULKAN "Enable Vulkan hardware renderer" ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(Dependencies)

set(PURERETRO_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cli
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/input
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io
    ${CMAKE_CURRENT_SOURCE_DIR}/src/logging
    ${CMAKE_CURRENT_SOURCE_DIR}/src/video
)

# Source files
set(PURERETRO_SOURCES
    src/app/main.c
    src/app/frontend_lifecycle.c
    src/app/run_loop.c
    src/audio/audio.c
    src/cli/cli.c
    src/core/core.c
    src/core/core_callbacks.c
    src/core/core_content.c
    src/core/core_env_basic.c
    src/core/core_env_options.c
    src/core/core_env_runtime.c
    src/core/core_env_storage.c
    src/core/core_environment.c
    src/core/core_persistence.c
    src/core/core_storage.c
    src/core/core_variables.c
    src/core/core_variables_parse.c
    src/input/input.c
    src/io/vfs.c
    src/logging/log.c
    src/video/video.c
    src/video/video_sw.c
    src/video/video_gl.c
)

set(PURERETRO_HEADERS
    src/app/frontend.h
    src/app/frontend_lifecycle.h
    src/app/run_loop.h
    src/audio/audio.h
    src/cli/cli.h
    src/core/core.h
    src/core/core_content.h
    src/core/core_internal.h
    src/core/core_variables.h
    src/core/core_variables_parse.h
    src/input/input.h
    src/io/vfs.h
    src/logging/log.h
    src/video/video.h
    src/video/video_backend.h
    src/video/video_sw.h
    src/video/video_gl.h
)

if(PURERETRO_ENABLE_VULKAN)
    list(APPEND PURERETRO_SOURCES
        src/video/video_vk.c
        src/video/video_vk_device.c
        src/video/video_vk_frame.c
        src/video/video_vk_present.c
        src/video/video_vk_swapchain.c
    )
    list(APPEND PURERETRO_HEADERS
        src/video/video_vk.h
        src/video/video_vk_internal.h
    )
endif()

add_executable(pureretro ${PURERETRO_SOURCES} ${PURERETRO_HEADERS})

target_include_directories(pureretro PRIVATE ${PURERETRO_INCLUDE_DIRS})

target_link_libraries(pureretro PRIVATE SDL3::SDL3)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(pureretro PRIVATE opengl32)
elseif(APPLE)
    find_library(OPENGL_LIBRARY OpenGL)
    if(OPENGL_LIBRARY)
        target_link_libraries(pureretro PRIVATE ${OPENGL_LIBRARY})
    endif()
else()
    find_package(OpenGL)
    if(OpenGL_FOUND)
        target_link_libraries(pureretro PRIVATE OpenGL::GL)
    endif()
endif()

if(PURERETRO_ENABLE_VULKAN)
    find_package(Vulkan)
    if(Vulkan_FOUND)
        target_link_libraries(pureretro PRIVATE Vulkan::Vulkan)
        target_compile_definitions(pureretro PRIVATE PURERETRO_VULKAN_ENABLED)
    else()
        message(WARNING "Vulkan SDK not found. Vulkan renderer will be stubbed.")
    endif()
endif()

# Compiler warnings
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pureretro PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wmissing-prototypes
        -Wstrict-prototypes
    )
elseif(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(pureretro PRIVATE /W4)
endif()

# Install
install(TARGETS pureretro DESTINATION bin)

if(PURERETRO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
