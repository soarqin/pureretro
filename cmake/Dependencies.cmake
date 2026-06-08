include(${CMAKE_CURRENT_LIST_DIR}/CPM.cmake)

option(PURERETRO_PREFER_SYSTEM_SDL3 "Prefer system-installed SDL3 over CPM" OFF)
option(PURERETRO_BUILD_TESTS "Build unit tests" ON)

# ---- SDL3 ----
set(_sdl3_resolved OFF)
if(PURERETRO_PREFER_SYSTEM_SDL3)
    find_package(SDL3 CONFIG QUIET)
    if(SDL3_FOUND)
        set(_sdl3_resolved ON)
        message(STATUS "Using system SDL3")
    endif()
endif()

if(NOT _sdl3_resolved)
    message(STATUS "Fetching SDL3 via CPM")
    set(SDL_TESTS    OFF CACHE BOOL "" FORCE)
    set(SDL_SHARED   OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC   ON  CACHE BOOL "" FORCE)
    CPMAddPackage(
        NAME       SDL3
        VERSION    3.2.4
        URL        https://github.com/libsdl-org/SDL/releases/download/release-3.2.4/SDL3-3.2.4.tar.gz
        URL_HASH   SHA256=2938328317301dfbe30176d79c251733aa5e7ec5c436c800b99ed4da7adcb0f0
    )
endif()

# ---- Unity (for tests) ----
if(PURERETRO_BUILD_TESTS)
    CPMAddPackage(
        NAME    Unity
        VERSION 2.6.0
        GITHUB_REPOSITORY ThrowTheSwitch/Unity
        OPTIONS "UNITY_EXTENSION_FIXTURE OFF" "UNITY_EXTENSION_MEMORY OFF"
    )
endif()
