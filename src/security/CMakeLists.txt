# Security Module CMakeLists.txt
# Anti-Tamper, Anti-Debug, String Obfuscation

# Anti-Tamper / Integrity Check
add_library(omnighost_anti_tamper STATIC
    anti_tamper.cpp
    integrity_check.cpp
    crc32.cpp
)

target_include_directories(omnighost_anti_tamper PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(omnighost_anti_tamper PRIVATE
    OMNIGHOST_SECURITY_BUILD
)

# Anti-Debug / Anti-Dump
add_library(omnighost_anti_debug STATIC
    anti_debug.cpp
    anti_dump.cpp
    hardware_bp.cpp
    timing_check.cpp
    heap_check.cpp
    peb_check.cpp
    self_crc.cpp
)

target_include_directories(omnighost_anti_debug PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(omnighost_anti_debug PRIVATE
    OMNIGHOST_SECURITY_BUILD
)

# String Obfuscation
add_library(omnighost_string_obfuscation STATIC
    string_obfuscation.cpp
    xorstr.cpp
    compile_time_hash.cpp
)

target_include_directories(omnighost_string_obfuscation PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(omnighost_string_obfuscation PRIVATE
    OMNIGHOST_SECURITY_BUILD
)

# Main security library that combines all
add_library(omnighost_security STATIC)
target_link_libraries(omnighost_security PUBLIC
    omnighost_anti_tamper
    omnighost_anti_debug
    omnighost_string_obfuscation
)

target_include_directories(omnighost_security PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Install headers
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION include/omnighost/security
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)