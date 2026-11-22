# Advanced Config Manager CMakeLists.txt
# Backup/Restore, Cloud Sync, Game Profiles

add_library(omnighost_config_advanced STATIC
    config_manager_advanced.cpp
    config_backup.cpp
    config_cloud_sync.cpp
    game_profile_manager.cpp
    config_migration.cpp
)

target_include_directories(omnighost_config_advanced PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../
)

target_link_libraries(omnighost_config_advanced PRIVATE
    omnighost_security
    nlohmann_json::nlohmann_json
    fmt::fmt
    spdlog::spdlog
)

if (WIN32)
    target_link_libraries(omnighost_config_advanced PRIVATE
        winhttp
        bcrypt
        crypt32
    )
endif()

# Install headers
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION include/omnighost/config/advanced
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)