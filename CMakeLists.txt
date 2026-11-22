cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

# =============================================================================
# OmniGhost - Modern CMake Build System (FetchContent Only - No vcpkg)
# =============================================================================
project(OmniGhost
    VERSION 1.3.4
    LANGUAGES CXX
    DESCRIPTION "OmniGhost DMA Overlay - Premium External Cheat Framework"
    HOMEPAGE_URL "https://omnighost.dev"
)

# =============================================================================
# Policies - Clean modern CMake
# =============================================================================
cmake_policy(SET CMP0077 NEW)  # option() honors normal variables
cmake_policy(SET CMP0091 NEW)  # Find modules honor EXCLUDE_FROM_ALL
cmake_policy(SET CMP0135 NEW)  # FetchContent timestamps
cmake_policy(SET CMP0154 NEW)  # target_link_libraries() with generator expressions

# Suppress all developer warnings from dependencies
set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON)
set(CMAKE_SUPPRESS_REGENERATION_WARNINGS ON)

# =============================================================================
# Cache Configuration for FetchContent
# =============================================================================
set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps_cache" CACHE PATH "FetchContent cache directory")
set(FETCHCONTENT_QUIET ON)
set(FETCHCONTENT_DOWNLOAD_EXTRACT_TIMESTAMP ON)
set(FETCHCONTENT_UPDATES_ALLOWED OFF)

# =============================================================================
# Policies & Standards
# =============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC: Warnings
if (MSVC)
    # KeyAuth's API objects were built against the dynamic MSVC runtime. Keep
    # OmniGhost and every FetchContent dependency on that same runtime.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    if (CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "Publish")
        add_compile_options(/W4 /WX /MP8 /permissive- /Zc:__cplusplus /Zc:preprocessor /Zc:inline /Zc:externConstexpr /wd4710 /wd4711 /wd4514 /wd4820 /wd4100 /wd4201 /wd4244 /wd4267 /wd4365 /wd4625 /wd4626 /wd5026 /wd5027 /wd5039 /wd5045)
    else()
        add_compile_options(/W4 /MP8 /permissive- /Zc:__cplusplus /Zc:preprocessor /Zc:inline /Zc:externConstexpr /wd4710 /wd4711 /wd4514 /wd4820 /wd4100 /wd4201 /wd4244 /wd4267 /wd4365 /wd4625 /wd4626 /wd5026 /wd5027 /wd5039 /wd5045)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror=return-type -Werror=uninitialized)
endif()

# Security hardening
if (MSVC)
    # /DYNAMICBASE and /NXCOMPAT are linker switches, not compiler switches.
    # The bundled KeyAuth binary predates /guard:ehcont.  Enabling it for our
    # objects makes the linker reject that trusted, prebuilt dependency.  CFG
    # remains enabled; EH continuation metadata must only be enabled when every
    # static dependency is rebuilt with the same toolchain switch.
    add_compile_options(/guard:cf)
    # SAFESEH is unsupported by x64 linkers.
    add_link_options(/DYNAMICBASE /NXCOMPAT)

    # CMake/MSBuild can select an SDK installation that only contains
    # libraries. Use an installed SDK containing the UCRT headers and make it
    # available to every target, including FetchContent dependencies.
    file(GLOB _omnighost_sdk_include_candidates LIST_DIRECTORIES true
        "C:/Program Files (x86)/Windows Kits/10/Include/*")
    foreach (_omnighost_sdk_include_candidate IN LISTS _omnighost_sdk_include_candidates)
        if (EXISTS "${_omnighost_sdk_include_candidate}/ucrt/crtdbg.h")
            set(_omnighost_windows_sdk_include "${_omnighost_sdk_include_candidate}")
        endif()
    endforeach()
    if (_omnighost_windows_sdk_include)
        add_compile_options(
            "/I${_omnighost_windows_sdk_include}/ucrt"
            "/I${_omnighost_windows_sdk_include}/shared"
            "/I${_omnighost_windows_sdk_include}/um"
            "/I${_omnighost_windows_sdk_include}/winrt"
            "/I${_omnighost_windows_sdk_include}/cppwinrt")
        # Extract Windows SDK version from the path (e.g., .../Include/10.0.22621.0 -> 10.0.22621.0)
        get_filename_component(_omnighost_windows_sdk_version "${_omnighost_windows_sdk_include}" NAME)
        set(OMNIGHOST_WINDOWS_SDK_VERSION "${_omnighost_windows_sdk_version}" CACHE INTERNAL "Detected Windows SDK version")
        message(STATUS "Windows SDK version: ${OMNIGHOST_WINDOWS_SDK_VERSION}")
    else()
        message(FATAL_ERROR "No installed Windows SDK contains the UCRT headers.")
    endif()
else()
    add_compile_options(-fstack-protector-strong -D_FORTIFY_SOURCE=2)
    add_link_options(-Wl,-z,relro,-z,now)
endif()

# =============================================================================
# Check for Python (required for custom targets)
# =============================================================================
find_program(PYTHON_EXECUTABLE python python3)
if (PYTHON_EXECUTABLE)
    set(HAVE_PYTHON TRUE)
    message(STATUS "Python found: ${PYTHON_EXECUTABLE}")
else()
    set(HAVE_PYTHON FALSE)
    message(STATUS "Python not found; optional maintenance targets are disabled.")
endif()

# =============================================================================
# FetchContent Dependencies (ALL - No vcpkg)
# =============================================================================
include(FetchContent)
set(FETCHCONTENT_QUIET ON)
set(FETCHCONTENT_DOWNLOAD_EXTRACT_TIMESTAMP ON)

# nlohmann_json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
)

# fmt
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 10.2.1
)

# spdlog - disable tests and external fmt
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
    CMAKE_ARGS
        -DGLFW_BUILD_EXAMPLES=OFF
        -DGLFW_BUILD_TESTS=OFF
        -DGLFW_BUILD_DOCS=OFF
)

# Configure spdlog before FetchContent_MakeAvailable
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
set(SPDLOG_NO_EXCEPTIONS ON CACHE BOOL "" FORCE)
set(EXPECTED_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)

# Suppress imgui warnings
set(IMGUI_DISABLE_OBSOLETE_FUNCTIONS 1)

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
    )
endif()

if (NOT TARGET glfw3::glfw3)
    add_library(glfw3::glfw3 INTERFACE IMPORTED)
    set_target_properties(glfw3::glfw3 PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${glfw_SOURCE_DIR}/include"
        INTERFACE_LINK_LIBRARIES "glfw"
    )
endif()

# Suppress spdlog catch2 dependency
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)

# Suppress imgui warnings
set(IMGUI_DISABLE_OBSOLETE_FUNCTIONS 1)

message(STATUS "Dependencies: FetchContent (ALL)")

# =============================================================================
# Build Configurations
# =============================================================================
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Development;Publish" CACHE STRING "" FORCE)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# Final artefacts belong at the repository root; CMake's generated project,
# objects and dependency cache stay under .cache\cmake.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/build/debug")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/build/release")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEVELOPMENT "${CMAKE_SOURCE_DIR}/build/development")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_PUBLISH "${CMAKE_SOURCE_DIR}/build/publish")

# Map CMake build types to release channels for build metadata
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(OMNIGHOST_RELEASE_CHANNEL "development")
elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
    set(OMNIGHOST_RELEASE_CHANNEL "beta")
elseif (CMAKE_BUILD_TYPE STREQUAL "Development")
    set(OMNIGHOST_RELEASE_CHANNEL "development")
elseif (CMAKE_BUILD_TYPE STREQUAL "Publish")
    set(OMNIGHOST_RELEASE_CHANNEL "stable")
else()
    set(OMNIGHOST_RELEASE_CHANNEL "unknown")
endif()

# =============================================================================
# Reproducible Build Support
# =============================================================================
if (DEFINED ENV{SOURCE_DATE_EPOCH})
    string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%S" UTC TIMESTAMP ${SOURCE_DATE_EPOCH})
    add_compile_definitions(BUILD_TIMESTAMP="${BUILD_TIMESTAMP}")
    message(STATUS "SOURCE_DATE_EPOCH set: ${SOURCE_DATE_EPOCH}")
else()
    string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%S" UTC)
    add_compile_definitions(BUILD_TIMESTAMP="${BUILD_TIMESTAMP}")
endif()

if (MSVC)
    add_compile_options(/Brepro)
endif()

# =============================================================================
# Platform Definitions
# =============================================================================
add_compile_definitions(
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    IMGUI_DISABLE_OBSOLETE_FUNCTIONS
)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(OMNIGHOST_DEV_EXTERNAL_OFFSETS)
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Development")
    # Development is a separately authenticated, private update channel.  It
    # deliberately keeps the normal KeyAuth/website login instead of using a
    # local bypass, and can never select the public Stable channel at runtime.
    add_compile_definitions(OMNIGHOST_DEV_BUILD OMNIGHOST_PRIVATE_DEV_BUILD OMNIGHOST_KEYAUTH_ENABLED)
    set(OMNIGHOST_DEVELOPMENT_VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/project/config/release/development-version.txt")
    if (NOT EXISTS "${OMNIGHOST_DEVELOPMENT_VERSION_FILE}")
        message(FATAL_ERROR "Development version file missing: ${OMNIGHOST_DEVELOPMENT_VERSION_FILE}")
    endif()
    file(READ "${OMNIGHOST_DEVELOPMENT_VERSION_FILE}" OMNIGHOST_DEVELOPMENT_VERSION)
    string(STRIP "${OMNIGHOST_DEVELOPMENT_VERSION}" OMNIGHOST_DEVELOPMENT_VERSION)
    if (NOT OMNIGHOST_DEVELOPMENT_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+-dev\\.[0-9]+$")
        message(FATAL_ERROR "Development version must look like 1.3.5-dev.1, got '${OMNIGHOST_DEVELOPMENT_VERSION}'.")
    endif()
    add_compile_definitions(OMNIGHOST_BUILD_VERSION=\"${OMNIGHOST_DEVELOPMENT_VERSION}\" OMNIGHOST_BUILD_CHANNEL=\"Development\")
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Publish")
    # Keep the same entitlement-provider definition used by the legacy
    # Publish project configuration.
    add_compile_definitions(OMNIGHOST_PUBLISH_BUILD OMNIGHOST_KEYAUTH_ENABLED)
endif()

# =============================================================================
# Include Directories
# =============================================================================
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
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

include_directories(
    ${nlohmann_json_SOURCE_DIR}/include
    ${fmt_SOURCE_DIR}/include
    ${spdlog_SOURCE_DIR}/include
    ${tl_expected_SOURCE_DIR}/include
    ${imgui_SOURCE_DIR}
    ${glfw_SOURCE_DIR}/include
)

# =============================================================================
# Source Files
# =============================================================================
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

list(FILTER OMNIGHOST_SOURCES EXCLUDE REGEX ".*[Tt]est.*\\.cpp$")
list(FILTER OMNIGHOST_SOURCES EXCLUDE REGEX ".*example.*\\.cpp$")

# These directories contain optional work-in-progress modules.  Their own
# CMakeLists files are conditionally enabled above only when the complete
# implementation is present; do not pull incomplete sources into the main
# executable through the broad source glob.
list(FILTER OMNIGHOST_SOURCES EXCLUDE REGEX ".*/src/config/advanced/.*\\.cpp$")
list(FILTER OMNIGHOST_SOURCES EXCLUDE REGEX ".*/src/updater/auto/.*\\.cpp$")
list(FILTER OMNIGHOST_SOURCES EXCLUDE REGEX ".*/src/launcher/cloud_sync\\.cpp$")
# Build tools are separate executables/scripts and are not launcher sources.
# The broad recursive glob must not pull their standalone main.cpp files into
# the OmniGhost target.
list(FILTER OMNIGHOST_SOURCES EXCLUDE REGEX ".*/src/project/tools/.*\\.cpp$")
# The original widgets.cpp is the production implementation.  The split
# widget sources are an incomplete refactor that currently redeclares the same
# API with incompatible defaults; keep them available in the tree, but do not
# compile both implementations into the launcher.
list(FILTER OMNIGHOST_SOURCES EXCLUDE REGEX ".*/src/window/widgets_(core|card|input|layout)\\.cpp$")
# The extended localization editor is not referenced by the launcher and is
# an unfinished parallel implementation of localization.cpp.
list(FILTER OMNIGHOST_SOURCES EXCLUDE REGEX ".*/src/window/localization_extended\\.cpp$")
# json-schema-validator is not a FetchContent dependency. The launcher does
# not consume this optional validation service at runtime.
list(FILTER OMNIGHOST_SOURCES EXCLUDE REGEX ".*/src/config/config_schema_validator\\.cpp$")
# config_profiles is an unfinished parallel profile implementation.  The active
# configuration path is config_manager.cpp; compiling both adds an unused,
# incompatible API surface to the shipping target.
list(FILTER OMNIGHOST_SOURCES EXCLUDE REGEX ".*/src/config/config_profiles\\.cpp$")
# The standalone anti-tamper/anti-debug implementation is not integrated with
# SecurityManager and currently has a conflicting legacy API.  Keep its source
# available for the dedicated security target, but do not compile it into the
# launcher through the broad glob.
list(FILTER OMNIGHOST_SOURCES EXCLUDE REGEX ".*/src/security/(anti_debug|anti_tamper|anti_dump|hardware_bp|timing_check|heap_check|peb_check|self_crc|integrity_check|crc32|string_obfuscation|xorstr|compile_time_hash)\\.cpp$")
# Keep KeyAuth's QR encoder: auth.hpp calls it to create the activation QR
# image.  Only its optional PNG/network helpers are excluded; they are not
# used by OmniGhost and are not warning-clean under the launcher's /WX policy.
list(FILTER OMNIGHOST_SOURCES EXCLUDE REGEX ".*/src/project/third_party/keyauth/.*/QRCode/(pingout|qr\\.png)\\.cpp$")
# OmniGhost supplies get_hwid() through keyauth_hwid_override.cpp.  Do not also
# compile KeyAuth's bundled utils.cpp, which defines the same symbol.
list(FILTER OMNIGHOST_SOURCES EXCLUDE REGEX ".*/src/project/third_party/keyauth/.*/utils\\.cpp$")

# =============================================================================
# Resources
# =============================================================================
if (WIN32)
    set(RESOURCE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/src/project/resources/app.rc)
    set(ICON_FILE ${CMAKE_CURRENT_SOURCE_DIR}/src/project/resources/OmniGhost.ico)
endif()

# =============================================================================
# Main Executable
# =============================================================================
add_executable(OmniGhost WIN32 ${OMNIGHOST_SOURCES} ${RESOURCE_FILE})

# KeyAuth ships a vendored, older nlohmann/json copy.  Treat only that SDK as
# external so MSVC's /WX policy continues to protect OmniGhost source without
# failing the build on warnings originating inside third-party headers.
target_include_directories(OmniGhost SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/project/third_party/keyauth/x64
    ${CMAKE_CURRENT_SOURCE_DIR}/src/project/third_party/keyauth/x64/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/src/project/third_party/keyauth/x64/lib/libsodium
)

# Configuration serialization is only used for configuration I/O.  These
# translation units contain large JSON/dispatch tables where MSVC's Release
# optimizer costs minutes while providing no measurable runtime benefit.
file(GLOB _omnighost_config_sources "${CMAKE_CURRENT_SOURCE_DIR}/src/config/*.cpp")
set_source_files_properties(${_omnighost_config_sources}
    PROPERTIES COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:MSVC>:/Od>")

set_target_properties(OmniGhost PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    OUTPUT_NAME "OmniGhost"
    DEBUG_POSTFIX "_d"
    # Build artefacts are intentionally separate from CMake's generated cache.
    # With the Visual Studio multi-config generator this expands, for example,
    # to build\\publish\\OmniGhost.exe or build\\release\\OmniGhost.exe.
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build/$<LOWER_CASE:$<CONFIG>>"
    # app.rc owns manifest resource ID 1; prevent MSVC/CMake from embedding a
    # second generated manifest with the same resource ID.
    VS_MANIFEST_TO_EMBED FALSE
)

target_compile_options(OmniGhost PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>
    $<$<CXX_COMPILER_ID:MSVC>:/Zc:inline>
    $<$<CXX_COMPILER_ID:MSVC>:/Zc:externConstexpr>
    $<$<CXX_COMPILER_ID:MSVC>:/external:W0>
    $<$<CXX_COMPILER_ID:MSVC>:/external:I"${CMAKE_CURRENT_SOURCE_DIR}/src/project/third_party/dma_stack/include">
)

# Supply the application manifest to the linker so it is merged into the one
# manifest CMake/MSBuild embeds. app.rc must not embed a second ID-1 manifest.
if (MSVC)
    target_link_options(OmniGhost PRIVATE
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/src/project/resources/app.manifest"
        # Some archived KeyAuth helper objects still request LIBCMT even though
        # the API objects use the dynamic runtime.  Do not link a second CRT;
        # resolve them through the single /MD runtime selected above.
        "/NODEFAULTLIB:LIBCMT"
    )
endif()

# Link libraries (FetchContent + Windows)
target_link_libraries(OmniGhost PRIVATE
    nlohmann_json::nlohmann_json
    fmt::fmt
    spdlog::spdlog
    glfw
)

if (WIN32)
    target_link_libraries(OmniGhost PRIVATE
        d3d11 dxgi d3dcompiler dwmapi user32 gdi32 imm32 setupapi ws2_32
        windowscodecs ole32 shell32 winhttp wininet bcrypt wintrust crypt32 shlwapi version advapi32 winmm
        delayimp
    )
    # The DMA import libraries are bundled with the project rather than
    # installed globally. Use their explicit canonical paths for CMake/MSBuild.
    target_link_libraries(OmniGhost PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/project/third_party/dma_stack/lib/leechcore.lib"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/project/third_party/dma_stack/lib/vmm.lib"
    )
    # KeyAuth's declarations are header-only, but the API implementation and
    # its crypto/HTTP dependencies are supplied as bundled import libraries.
    # Link them explicitly so Publish retains the configured entitlement
    # provider instead of leaving unresolved KeyAuth::api symbols.
    target_link_libraries(OmniGhost PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/project/third_party/keyauth/x64/library_x64.lib"
    )
endif()

# Build channel definition
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(OmniGhost PRIVATE OMNIGHOST_BUILD_CHANNEL="Development")
elseif (CMAKE_BUILD_TYPE STREQUAL "Development")
    target_compile_definitions(OmniGhost PRIVATE OMNIGHOST_BUILD_CHANNEL="Development")
elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(OmniGhost PRIVATE OMNIGHOST_BUILD_CHANNEL="Beta")
elseif (CMAKE_BUILD_TYPE STREQUAL "Publish")
    target_compile_definitions(OmniGhost PRIVATE OMNIGHOST_BUILD_CHANNEL="Stable")
endif()

# =============================================================================
# Security Targets (Anti-Tamper, Anti-Debug, String Obfuscation)
# =============================================================================
# The legacy security libraries have their own incompatible public API and are
# not linked by OmniGhost.  SecurityManager remains the active implementation.

# =============================================================================
# Embedded payload generation
# =============================================================================
# Resource compilation includes the generated .rc2 fragments and the runtime
# bootstrap includes its generated manifest header.  Generate them before the
# executable target and expose the configuration-specific cache directory to
# both C++ and RC compilation.
if (WIN32)
    find_program(OMNIGHOST_POWERSHELL_EXECUTABLE NAMES powershell pwsh)
    if (OMNIGHOST_POWERSHELL_EXECUTABLE)
        set(OMNIGHOST_GENERATED_DIR
            "${CMAKE_CURRENT_SOURCE_DIR}/.cache/generated/${CMAKE_BUILD_TYPE}/x64")

        add_custom_target(GenerateEmbeddedOffsets
            COMMAND "${OMNIGHOST_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/src/project/tools/Build-EmbeddedOffsets.ps1"
                -ProjectDir "${CMAKE_CURRENT_SOURCE_DIR}" -Configuration "${CMAKE_BUILD_TYPE}" -Platform x64
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMENT "Generating embedded offset payload"
        )
        add_custom_target(GenerateEmbeddedRuntime
            COMMAND "${OMNIGHOST_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/src/project/tools/Build-EmbeddedRuntime.ps1"
                -ProjectDir "${CMAKE_CURRENT_SOURCE_DIR}" -Configuration "${CMAKE_BUILD_TYPE}" -Platform x64
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMENT "Generating embedded runtime payload"
        )
        add_custom_target(GenerateEmbeddedResources
            COMMAND "${OMNIGHOST_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/src/project/tools/Build-EmbeddedResources.ps1"
                -ProjectDir "${CMAKE_CURRENT_SOURCE_DIR}" -Configuration "${CMAKE_BUILD_TYPE}" -Platform x64
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMENT "Generating embedded resource catalog"
        )

        # Generate build metadata (required for Publish configuration)
        add_custom_target(GenerateBuildMetadata
            COMMAND "${OMNIGHOST_POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/src/project/tools/Generate-BuildMetadata.ps1"
                -ProjectDir "${CMAKE_CURRENT_SOURCE_DIR}"
                -Configuration "${CMAKE_BUILD_TYPE}"
                -Architecture "x64"
                -ReleaseChannel "${OMNIGHOST_RELEASE_CHANNEL}"
                -WindowsSdkVersion "${OMNIGHOST_WINDOWS_SDK_VERSION}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMENT "Generating build metadata"
        )

        add_dependencies(OmniGhost GenerateEmbeddedOffsets GenerateEmbeddedRuntime GenerateEmbeddedResources GenerateBuildMetadata)
        target_include_directories(OmniGhost PRIVATE "${OMNIGHOST_GENERATED_DIR}")
    else()
        message(FATAL_ERROR "PowerShell is required to generate OmniGhost embedded resources.")
    endif()
endif()

# Custom Targets (only if Python is available)
# =============================================================================
if (HAVE_PYTHON)
    add_custom_target(ValidateOffsets
        COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/src/project/tools/Validate-Offsets.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Validating offset JSON files"
    )

    add_custom_target(ProtectStrings
        COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/src/project/tools/Protect-Strings.ps1 -Configuration $<CONFIG>
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Applying string protection"
    )

    add_custom_target(UpdateCloudflared
        COMMAND powershell -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/src/project/tools/Update-Cloudflared.ps1" -EnsureLatest
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Updating Cloudflared binary"
    )

    add_custom_target(UpdateDmaDependencies
        COMMAND powershell -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/src/project/tools/Update-DmaDependencies.ps1" -EnsureLatest
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Updating DMA stack dependencies"
    )

    if (CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "Development" OR CMAKE_BUILD_TYPE STREQUAL "Publish")
        add_dependencies(OmniGhost ProtectStrings)
        add_dependencies(OmniGhost ValidateOffsets)
    endif()
else()
    message(STATUS "Optional Python maintenance targets are disabled.")
endif()

# =============================================================================
# Advanced Config Manager (Backup/Restore, Cloud Sync, Game Profiles)
# =============================================================================
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/config/advanced/config_manager_advanced.cpp")
    add_subdirectory(src/config/advanced)
endif()

# =============================================================================
# Auto-Updater (GitHub Releases)
# =============================================================================
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/updater/auto/github_release_client.cpp")
    add_subdirectory(src/updater/auto)
endif()

# =============================================================================
# Cloud Sync for Configs
# =============================================================================
# cloud_sync is compiled through OMNIGHOST_SOURCES above.  Keep this optional
# so a source-only module cannot make the whole project fail configuration when
# it has not yet been split into its own CMake target.
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/launcher/cloud_sync/CMakeLists.txt")
    add_subdirectory(src/launcher/cloud_sync)
endif()

# =============================================================================
# Installation
# =============================================================================
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

# =============================================================================
# CPack for NSIS Installer
# =============================================================================
set(CPACK_PACKAGE_NAME "OmniGhost")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "OmniGhost DMA Overlay")
set(CPACK_PACKAGE_VENDOR "OmniGhost Team")
set(CPACK_PACKAGE_CONTACT "support@omnighost.dev")
set(CPACK_GENERATOR "NSIS")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-win64")
set(CPACK_NSIS_INSTALL_ROOT "OmniGhost")
set(CPACK_NSIS_MODIFY_PATH ON)
set(CPACK_NSIS_DISPLAY_NAME "OmniGhost ${PROJECT_VERSION}")
set(CPACK_NSIS_HELP_LINK "https://omnighost.dev")
set(CPACK_NSIS_URL_INFO_ABOUT "https://omnighost.dev")
set(CPACK_NSIS_CONTACT "support@omnighost.dev")
set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/src/project/resources/OmniGhost.ico")
set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/src/project/resources/OmniGhost.ico")
set(CPACK_NSIS_INSTALLED_ICON_NAME "OmniGhost.exe")
set(CPACK_NSIS_EXTRA_PREINSTALL_COMMANDS "")
set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "")
set(CPACK_NSIS_COMPRESSION "ZSTD")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_MODIFY_PATH ON)

include(CPack)

# =============================================================================
# CPack for Packaging (ZIP)
# =============================================================================
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-win64")

# =============================================================================
# Testing
# =============================================================================
enable_testing()

add_test(NAME OffsetValidation
    COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/src/project/tools/Validate-Offsets.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# =============================================================================
# Developer Helpers
# =============================================================================
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

# =============================================================================
# Print Configuration Summary (Clean)
# =============================================================================
message(STATUS "")
message(STATUS "OmniGhost Build Configuration")
message(STATUS "  Version:     ${PROJECT_VERSION}")
message(STATUS "  Build Type:  ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard: 20")
message(STATUS "  Generator:   ${CMAKE_GENERATOR}")
message(STATUS "  Dependencies: FetchContent (ALL)")
if (HAVE_PYTHON)
    message(STATUS "  Python:      ${PYTHON_EXECUTABLE}")
else()
    message(STATUS "  Python:      NOT FOUND (custom targets disabled)")
endif()
message(STATUS "")
