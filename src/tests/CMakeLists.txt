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

# CTest configuration
set(CTEST_OUTPUT_ON_FAILURE ON)
set(CTEST_PARALLEL_LEVEL 4)