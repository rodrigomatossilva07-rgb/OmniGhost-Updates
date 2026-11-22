# Auto-Updater CMakeLists.txt
# GitHub Releases auto-updater

add_library(omnighost_auto_updater STATIC
    auto_updater.cpp
    github_release_client.cpp
    update_installer.cpp
    update_verifier.cpp
)

target_include_directories(omnighost_auto_updater PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(omnighost_auto_updater PRIVATE
    omnighost_security
    omnighost_config_advanced
    nlohmann_json::nlohmann_json
    fmt::fmt
    spdlog::spdlog
)

if (WIN32)
    target_link_libraries(omnighost_auto_updater PRIVATE
        winhttp
        bcrypt
        crypt32
        version
        msi
    )
endif()

# Install headers
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION include/omnighost/updater/auto
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)