cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

# =============================================================================
# OmniGhost - Modern CMake Build System
# =============================================================================
project(OmniGhost
    VERSION 1.3.4
    LANGUAGES CXX
    DESCRIPTION "OmniGhost DMA Overlay - Premium External Cheat Framework"
    HOMEPAGE_URL "https://omnighost.dev"
)

# -----------------------------------------------------------------------------
# Policies & Standards
# -----------------------------------------------------------------------------
# Enforce modern C++ practices
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC: Treat warnings as errors in Release/Publish
if (MSVC)
    if (CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "Publish")
        add_compile_options(/W4 /WX /permissive- /Zc:__cplusplus /Zc:preprocessor /Zc:inline /Zc:externConstexpr)
    else()
        add_compile_options(/W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /Zc:inline /Zc:externConstexpr)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror=return-type -Werror=uninitialized)
endif()

# Security hardening
if (MSVC)
    add_compile_options(/guard:cf /guard:ehcont /DYNAMICBASE /NXCOMPAT)
    add_link_options(/DYNAMICBASE /NXCOMPAT /SAFESEH)
else()
    add_compile_options(-fstack-protector-strong -D_FORTIFY_SOURCE=2)
    add_link_options(-Wl,-z,relro,-z,now)
endif()

# -----------------------------------------------------------------------------
# Dependency Provider Selection
# -----------------------------------------------------------------------------
# Option to use FetchContent instead of vcpkg for dependencies
option(OMNIGHOST_USE_FETCHCONTENT "Use FetchContent for dependencies instead of vcpkg" OFF)
option(OMNIGHOST_FETCHCONTENT_FULLY_DISCONNECTED "Run FetchContent in fully disconnected mode (offline)" OFF)

if (OMNIGHOST_FETCHCONTENT_FULLY_DISCONNECTED)
    set(FETCHCONTENT_FULLY_DISCONNECTED ON)
endif()

# -----------------------------------------------------------------------------
# vcpkg Integration (default)
# -----------------------------------------------------------------------------
if (NOT OMNIGHOST_USE_FETCHCONTENT)
    # Expect vcpkg toolchain to be provided via -DCMAKE_TOOLCHAIN_FILE
    # or VCPKG_ROOT environment variable
    if (NOT DEFINED ENV{VCPKG_ROOT} AND NOT CMAKE_TOOLCHAIN_FILE)
        message(WARNING "vcpkg not detected. Set VCPKG_ROOT or pass -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake")
    endif()
endif()

# -----------------------------------------------------------------------------
# FetchContent Dependencies (alternative to vcpkg)
# -----------------------------------------------------------------------------
if (OMNIGHOST_USE_FETCHCONTENT)
    include(FetchContent)

    # nlohmann_json
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.2
    )

    # fmt
    FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 10.2.1
    )

    # spdlog
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.12.0
    )

    # tl::expected
    FetchContent_Declare(
        tl_expected
        GIT_REPOSITORY https://github.com/TartanLlama/expected.git
        GIT_TAG v1.1.0
    )

    # ImGui (docking branch)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG docking
    )

    # GLFW
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.3.8
    )

    # Make FetchContent available
    FetchContent_MakeAvailable(
        nlohmann_json
        fmt
        spdlog
        tl_expected
        imgui
        glfw
    )

    # Alias targets for consistency
    if (NOT TARGET nlohmann_json::nlohmann_json)
        add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
        set_target_properties(nlohmann_json::nlohmann_json PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${nlohmann_json_SOURCE_DIR}/include"
        )
    endif()

    if (NOT TARGET fmt::fmt)
        add_library(fmt::fmt INTERFACE IMPORTED)
        set_target_properties(fmt::fmt PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${fmt_SOURCE_DIR}/include"
        )
    endif()

    if (NOT TARGET spdlog::spdlog)
        add_library(spdlog::spdlog INTERFACE IMPORTED)
        set_target_properties(spdlog::spdlog PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${spdlog_SOURCE_DIR}/include"
        )
    endif()

    if (NOT TARGET ImGui::ImGui)
        add_library(ImGui::ImGui INTERFACE IMPORTED)
        set_target_properties(ImGui::ImGui PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${imgui_SOURCE_DIR}"
            INTERFACE_LINK_LIBRARIES "imgui"
        )
    endif()

    if (NOT TARGET glfw3::glfw3)
        add_library(glfw3::glfw3 INTERFACE IMPORTED)
        set_target_properties(glfw3::glfw3 PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${glfw_SOURCE_DIR}/include"
            INTERFACE_LINK_LIBRARIES "glfw"
        )
    endif()
endif()

# -----------------------------------------------------------------------------
# Find Dependencies (via vcpkg or FetchContent)
# -----------------------------------------------------------------------------
if (OMNIGHOST_USE_FETCHCONTENT)
    # Dependencies already available via FetchContent_MakeAvailable
    message(STATUS "Using FetchContent for dependencies")
else()
    find_package(nlohmann_json 3.11 REQUIRED CONFIG)
    find_package(fmt 10.0 REQUIRED CONFIG)
    find_package(spdlog 1.12 REQUIRED CONFIG)
    find_package(tl_expected 1.1 REQUIRED CONFIG)
    find_package(ImGui REQUIRED CONFIG)  # Custom vcpkg port
    find_package(glfw3 3.3 REQUIRED CONFIG)
    message(STATUS "Using vcpkg for dependencies")
endif()

# Windows-specific
if (WIN32)
    find_package(WindowsSDK REQUIRED)
    # DMALibrary/LeechCore are linked manually (not in vcpkg)
endif()

# -----------------------------------------------------------------------------
# Build Configurations
# -----------------------------------------------------------------------------
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Publish" CACHE STRING "" FORCE)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build)

# Per-config output
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/build/Debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/build/Release)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_PUBLISH ${CMAKE_BINARY_DIR}/build/Publish)

# -----------------------------------------------------------------------------
# Reproducible Build Support
# -----------------------------------------------------------------------------
# SOURCE_DATE_EPOCH for deterministic builds
if (DEFINED ENV{SOURCE_DATE_EPOCH})
    # Use SOURCE_DATE_EPOCH as build timestamp for reproducibility
    string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%S" UTC TIMESTAMP ${SOURCE_DATE_EPOCH})
    add_compile_definitions(BUILD_TIMESTAMP="${BUILD_TIMESTAMP}")
    message(STATUS "SOURCE_DATE_EPOCH set: ${SOURCE_DATE_EPOCH}, using deterministic timestamp")
else()
    string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%S" UTC)
    add_compile_definitions(BUILD_TIMESTAMP="${BUILD_TIMESTAMP}")
endif()

# Deterministic archive output (MSVC)
if (MSVC)
    add_compile_options(/Brepro)
endif()

# -----------------------------------------------------------------------------
# Platform Definitions
# -----------------------------------------------------------------------------
add_compile_definitions(
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    # OMNIGHOST_DEV_EXTERNAL_OFFSETS  // Removido: usar embedded offsets por padrão (single EXE)
)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Apenas debug builds podem usar offsets externos (desenvolvimento)
    add_compile_definitions(OMNIGHOST_DEV_EXTERNAL_OFFSETS)
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Publish")
    add_compile_definitions(OMNIGHOST_PUBLISH_BUILD)
endif()

# -----------------------------------------------------------------------------
# Include Directories
# -----------------------------------------------------------------------------
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/games
    ${CMAKE_CURRENT_SOURCE_DIR}/src/games/Cs2
    ${CMAKE_CURRENT_SOURCE_DIR}/src/games/Fivem
    ${CMAKE_CURRENT_SOURCE_DIR}/src/games/Warzone
    ${CMAKE_CURRENT_SOURCE_DIR}/src/games/Apex
    ${CMAKE_CURRENT_SOURCE_DIR}/src/games/Rust
    ${CMAKE_CURRENT_SOURCE_DIR}/src/games/Fortnite
    ${CMAKE_CURRENT_SOURCE_DIR}/src/project/third_party/ImGui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/project/third_party/DMALibrary
    ${CMAKE_CURRENT_SOURCE_DIR}/src/project/third_party/dma_stack/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/project/libs
    ${CMAKE_CURRENT_SOURCE_DIR}/src/project/third_party/Utils
    ${CMAKE_CURRENT_SOURCE_DIR}/src/project/resources
)

# Dependency includes (vcpkg or FetchContent)
if (OMNIGHOST_USE_FETCHCONTENT)
    include_directories(
        ${nlohmann_json_SOURCE_DIR}/include
        ${fmt_SOURCE_DIR}/include
        ${spdlog_SOURCE_DIR}/include
        ${tl_expected_SOURCE_DIR}/include
        ${imgui_SOURCE_DIR}
        ${glfw_SOURCE_DIR}/include
    )
else()
    include_directories(${nlohmann_json_INCLUDE_DIRS} ${fmt_INCLUDE_DIRS} ${spdlog_INCLUDE_DIRS} ${ImGui_INCLUDE_DIRS} ${glfw3_INCLUDE_DIRS})
endif()

# -----------------------------------------------------------------------------
# Source Files
# -----------------------------------------------------------------------------
file(GLOB_RECURSE OMNIGHOST_SOURCES
    "src/*.cpp"
    "src/**/*.cpp"
    "src/games/Cs2/**/*.cpp"
    "src/games/Fivem/**/*.cpp"
    "src/games/Warzone/**/*.cpp"
    "src/games/Apex/**/*.cpp"
    "src/games/Rust/**/*.cpp"
    "src/games/Fortnite/**/*.cpp"
    "src/project/third_party/ImGui/*.cpp"
    "src/project/third_party/DMALibrary/Memory/*.cpp"
    "src/project/third_party/Utils/*.cpp"
)

# Exclude test/example files
list(FILTER OMNIGHOST_SOURCES EXCLUDE REGEX ".*[Tt]est.*\\.cpp$")
list(FILTER OMNIGHOST_SOURCES EXCLUDE REGEX ".*example.*\\.cpp$")

# -----------------------------------------------------------------------------
# Resources
# -----------------------------------------------------------------------------
if (WIN32)
    set(RESOURCE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/src/project/resources/app.rc)
    set(ICON_FILE ${CMAKE_CURRENT_SOURCE_DIR}/src/project/resources/OmniGhost.ico)
endif()

# -----------------------------------------------------------------------------
# Main Executable
# -----------------------------------------------------------------------------
add_executable(OmniGhost WIN32 ${OMNIGHOST_SOURCES} ${RESOURCE_FILE})

# Target properties
set_target_properties(OmniGhost PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    OUTPUT_NAME "OmniGhost"
    DEBUG_POSTFIX "_d"
)

# Compile options per target
target_compile_options(OmniGhost PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>
    $<$<CXX_COMPILER_ID:MSVC>:/Zc:inline>
    $<$<CXX_COMPILER_ID:MSVC>:/Zc:externConstexpr>
    $<$<CXX_COMPILER_ID:MSVC>:/external:I"${CMAKE_CURRENT_SOURCE_DIR}/src/project/third_party/dma_stack/include">
)

# Link libraries
if (OMNIGHOST_USE_FETCHCONTENT)
    target_link_libraries(OmniGhost PRIVATE
        nlohmann_json::nlohmann_json
        fmt::fmt
        spdlog::spdlog
        imgui
        glfw
    )
else()
    target_link_libraries(OmniGhost PRIVATE
        ${nlohmann_json_LIBRARIES}
        ${fmt_LIBRARIES}
        ${spdlog_LIBRARIES}
        ${ImGui_LIBRARIES}
        ${glfw3_LIBRARIES}
    )
endif()

if (WIN32)
    target_link_libraries(OmniGhost PRIVATE
        d3d11 dxgi d3dcompiler dwmapi user32 gdi32 imm32 setupapi ws2_32
        windowscodecs ole32 shell32 winhttp wininet bcrypt wintrust crypt32 shlwapi version advapi32 winmm
        delayimp
    )
    # DMALibrary/LeechCore - linked via pragmas in code or manually
    target_link_libraries(OmniGhost PRIVATE leechcore vmm)
endif()

# -----------------------------------------------------------------------------
# Custom Targets
# -----------------------------------------------------------------------------

# Embedded resources generation
add_custom_target(GenerateEmbeddedResources
    COMMAND ${CMAKE_COMMAND} -E echo "Generating embedded resources..."
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/src/project/tools/generate_embedded_resources.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating embedded resource catalog"
)

# Offset validation
add_custom_target(ValidateOffsets
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/src/project/tools/Validate-Offsets.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Validating offset JSON files"
)

# String protection (Release/Publish only)
add_custom_target(ProtectStrings
    COMMAND powershell -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/src/project/tools/Protect-Strings.ps1" -Configuration $<CONFIG>
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Applying string protection"
)

# Cloudflared update
add_custom_target(UpdateCloudflared
    COMMAND powershell -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/src/project/tools/Update-Cloudflared.ps1" -EnsureLatest
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Updating Cloudflared binary"
)

# DMA dependencies update
add_custom_target(UpdateDmaDependencies
    COMMAND powershell -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/src/project/tools/Update-DmaDependencies.ps1" -EnsureLatest
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Updating DMA stack dependencies"
)

# -----------------------------------------------------------------------------
# Build Dependencies
# -----------------------------------------------------------------------------
add_dependencies(OmniGhost GenerateEmbeddedResources)

if (CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "Publish")
    add_dependencies(OmniGhost ProtectStrings)
    add_dependencies(OmniGhost ValidateOffsets)
endif()

# -----------------------------------------------------------------------------
# Installation
# -----------------------------------------------------------------------------
install(TARGETS OmniGhost
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/project/resources/
    DESTINATION resources
    FILES_MATCHING PATTERN "*.png" PATTERN "*.ico" PATTERN "*.pdf"
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/project/config/release/
    DESTINATION config/release
    FILES_MATCHING PATTERN "*.json"
)

# -----------------------------------------------------------------------------
# CPack for Packaging
# -----------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "OmniGhost")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "OmniGhost DMA Overlay")
set(CPACK_PACKAGE_VENDOR "OmniGhost Team")
set(CPACK_PACKAGE_CONTACT "support@omnighost.dev")
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-win64")

include(CPack)

# -----------------------------------------------------------------------------
# Testing
# -----------------------------------------------------------------------------
enable_testing()

add_test(NAME OffsetValidation
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/src/project/tools/Validate-Offsets.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# -----------------------------------------------------------------------------
# Developer Helpers
# -----------------------------------------------------------------------------
add_custom_target(FormatCode
    COMMAND clang-format -i $(git ls-files '*.cpp' '*.h' '*.hpp' '*.inl')
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Formatting source code with clang-format"
)

add_custom_target(CheckFormat
    COMMAND clang-format --dry-run --Werror $(git ls-files '*.cpp' '*.h' '*.hpp' '*.inl')
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Checking code format"
)

# -----------------------------------------------------------------------------
# Print Configuration Summary
# -----------------------------------------------------------------------------
message(STATUS "================================================================")
message(STATUS "OmniGhost Build Configuration")
message(STATUS "================================================================")
message(STATUS "Version:           ${PROJECT_VERSION}")
message(STATUS "Build Type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard:      20")
message(STATUS "Generator:         ${CMAKE_GENERATOR}")
message(STATUS "Dependency Mode:   ${OMNIGHOST_USE_FETCHCONTENT}")
message(STATUS "vcpkg:             ${VCPKG_ROOT}")
message(STATUS "SOURCE_DATE_EPOCH: $ENV{SOURCE_DATE_EPOCH}")
message(STATUS "================================================================")