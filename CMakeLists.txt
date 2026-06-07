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

# -----------------------------------------------------------------------------
# SDL3 — try system first, fallback to FetchContent
# -----------------------------------------------------------------------------
find_package(SDL3 CONFIG QUIET)

if(NOT SDL3_FOUND)
    message(STATUS "SDL3 not found on system, fetching from source...")
    include(FetchContent)
    FetchContent_Declare(
        SDL3
        URL       https://github.com/libsdl-org/SDL/releases/download/release-3.2.4/SDL3-3.2.4.tar.gz
        URL_HASH  SHA256=2938328317301dfbe30176d79c251733aa5e7ec5c436c800b99ed4da7adcb0f0
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(SDL_TESTS    OFF CACHE BOOL "" FORCE)
    set(SDL_SHARED   OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC   ON  CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3)
endif()

# Source files
set(PURERETRO_SOURCES
    src/main.c
    src/core.c
    src/core_variables.c
    src/core_variables_parse.c
    src/video.c
    src/video_sw.c
    src/video_gl.c
    src/audio.c
    src/input.c
    src/vfs.c
    src/log.c
)

set(PURERETRO_HEADERS
    src/frontend.h
    src/core.h
    src/core_variables.h
    src/core_variables_parse.h
    src/video.h
    src/video_backend.h
    src/video_sw.h
    src/video_gl.h
    src/audio.h
    src/input.h
    src/vfs.h
    src/log.h
)

if(PURERETRO_ENABLE_VULKAN)
    list(APPEND PURERETRO_SOURCES src/video_vk.c)
    list(APPEND PURERETRO_HEADERS src/video_vk.h)
endif()

add_executable(pureretro ${PURERETRO_SOURCES} ${PURERETRO_HEADERS})

target_include_directories(pureretro PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

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
