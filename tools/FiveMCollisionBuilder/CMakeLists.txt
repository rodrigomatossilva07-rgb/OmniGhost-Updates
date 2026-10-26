cmake_minimum_required(VERSION 3.20)
project(FiveMCollisionBuilder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(RAGEKIT_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(RAGEKIT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(RAGEKIT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(RAGEKIT_BUILD_RENDER OFF CACHE BOOL "" FORCE)

if(DEFINED RAGEKIT_ROOT AND EXISTS "${RAGEKIT_ROOT}/CMakeLists.txt")
    add_subdirectory("${RAGEKIT_ROOT}" "${CMAKE_BINARY_DIR}/ragekit")
else()
    include(FetchContent)
    FetchContent_Declare(
        ragekit
        GIT_REPOSITORY https://github.com/RatPrez/ragekit.git
        GIT_TAG v0.3.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(ragekit)
endif()

add_executable(FiveMCollisionBuilder main.cpp)
target_link_libraries(FiveMCollisionBuilder PRIVATE ragekit::ragekit)

if(MSVC)
    target_compile_options(FiveMCollisionBuilder PRIVATE /W4 /WX /permissive- /utf-8)
else()
    target_compile_options(FiveMCollisionBuilder PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

