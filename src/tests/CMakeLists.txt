cmake_minimum_required(VERSION 3.25)

project(OmniGhostTests LANGUAGES CXX)

# Catch2
find_package(Catch2 3 REQUIRED)

# Enable testing
enable_testing()

# Test executable
add_executable(omnighost_tests
    test_offset_auto.cpp
    test_config_manager.cpp
    test_security_manager.cpp
    test_offset_manager.cpp
    test_config_schema_validator.cpp
    test_dma_thread_pool.cpp
)

# Link libraries
target_link_libraries(omnighost_tests PRIVATE
    Catch2::Catch2WithMain
    OmniGhost
)

# Include directories
target_include_directories(omnighost_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src
)

# Add test
add_test(NAME omnighost_unit_tests
    COMMAND omnighost_tests
)

# Fuzzer targets (only build with sanitizers)
if (CMAKE_CXX_FLAGS MATCHES "sanitize" OR CMAKE_CXX_FLAGS_DEBUG MATCHES "sanitize")
    # Fuzzer for offset blob parsing
    add_executable(fuzz_offset_parser
        fuzz_offset_parser.cpp
    )
    target_link_libraries(fuzz_offset_parser PRIVATE
        OmniGhost
        ${CMAKE_DL_LIBS}
    )
    target_compile_options(fuzz_offset_parser PRIVATE
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
        -g
    )
    set_target_properties(fuzz_offset_parser PROPERTIES
        LINK_FLAGS "-fsanitize=fuzzer,address,undefined"
    )
    
    # Fuzzer for JSON offset parsing
    add_executable(fuzz_json_offset
        fuzz_json_offset.cpp
    )
    target_link_libraries(fuzz_json_offset PRIVATE
        OmniGhost
        ${CMAKE_DL_LIBS}
    )
    target_compile_options(fuzz_json_offset PRIVATE
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
        -g
    )
    set_target_properties(fuzz_json_offset PROPERTIES
        LINK_FLAGS "-fsanitize=fuzzer,address,undefined"
    )
    
    # Fuzzer for config validation
    add_executable(fuzz_config
        fuzz_config.cpp
    )
    target_link_libraries(fuzz_config PRIVATE
        OmniGhost
        ${CMAKE_DL_LIBS}
    )
    target_compile_options(fuzz_config PRIVATE
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
        -g
    )
    set_target_properties(fuzz_config PROPERTIES
        LINK_FLAGS "-fsanitize=fuzzer,address,undefined"
    )
    
    # Fuzzer for schema validation
    add_executable(fuzz_schema
        fuzz_schema.cpp
    )
    target_link_libraries(fuzz_schema PRIVATE
        OmniGhost
        ${CMAKE_DL_LIBS}
    )
    target_compile_options(fuzz_schema PRIVATE
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
        -g
    )
    set_target_properties(fuzz_schema PROPERTIES
        LINK_FLAGS "-fsanitize=fuzzer,address,undefined"
    )
endif()

# Include directories
target_include_directories(omnighost_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/src
)

# Add test
add_test(NAME omnighost_unit_tests
    COMMAND omnighost_tests
)

# CTest configuration
set(CTEST_OUTPUT_ON_FAILURE ON)
set(CTEST_PARALLEL_LEVEL 4)