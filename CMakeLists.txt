cmake_minimum_required(VERSION 3.28)

project(AsteriaStudio
    VERSION 0.1.0
    DESCRIPTION "A complete computer system - ISA to OS - built in software"
    LANGUAGES CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use a preset: cmake --preset debug")
endif()

option(ASTERIA_BUILD_TESTS "Build the test suite" ON)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(AsteriaOptions)   # asteria::options - language standard and warnings for every target
include(AsteriaModule)    # asteria_add_module() / asteria_add_test()

add_subdirectory(src)
add_subdirectory(apps)

if(ASTERIA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
