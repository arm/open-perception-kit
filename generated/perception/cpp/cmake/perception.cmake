################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################
# Generated file. Do not edit.
# SDK users: change schemas or generator inputs, then regenerate this file.
#
# This file exposes a pre-generated perception C++ SDK to CMake consumers.
# Schema changes do NOT trigger regeneration during the build. Re-run your
# project SDK generation script before configuring or rebuilding consumers.
#
# Usage in your CMakeLists.txt:
#
#   include(path/to/generated/cpp/cmake/perception.cmake)
#   perception_enable_sdk()
#   target_link_libraries(my_app PRIVATE perception::sdk)
#
# Overridable variables (set BEFORE including this file):
#   PERCEPTION_FLATBUFFERS_INCLUDE_DIR - FlatBuffers headers (auto-discovered)
#
get_filename_component(_PERCEPTION_SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(PERCEPTION_VERSION "0.1.0")
set(PERCEPTION_FLATBUFFERS_VERSION_REQUIREMENT "==25.9.23")
set(PERCEPTION_PYTHON_VERSION_REQUIREMENT ">=3.10")
set(PERCEPTION_PYTHON_BRIDGE_AVAILABLE TRUE)
set(PERCEPTION_PYTHON_BRIDGE_MODULE "perception_bridge")

function(perception_enable_sdk)
    if(TARGET perception::sdk)
        message(FATAL_ERROR "perception_enable_sdk() has already been called")
    endif()

    if(NOT DEFINED PERCEPTION_FLATBUFFERS_INCLUDE_DIR OR PERCEPTION_FLATBUFFERS_INCLUDE_DIR STREQUAL "")
        find_path(PERCEPTION_FLATBUFFERS_INCLUDE_DIR flatbuffers/flatbuffers.h REQUIRED)
    endif()

    include(CheckCXXSourceCompiles)
    set(_PERCEPTION_SAVED_REQUIRED_INCLUDES "${CMAKE_REQUIRED_INCLUDES}")
    set(CMAKE_REQUIRED_INCLUDES "${PERCEPTION_FLATBUFFERS_INCLUDE_DIR}")
    check_cxx_source_compiles(
        "#include <flatbuffers/base.h>
     static_assert(FLATBUFFERS_VERSION_MAJOR == 25);
     static_assert(FLATBUFFERS_VERSION_MINOR == 9);
     static_assert(FLATBUFFERS_VERSION_REVISION == 23);
     int main() { return 0; }"
        _PERCEPTION_FLATBUFFERS_25_9_23_MATCHES)
    set(CMAKE_REQUIRED_INCLUDES "${_PERCEPTION_SAVED_REQUIRED_INCLUDES}")
    if(NOT _PERCEPTION_FLATBUFFERS_25_9_23_MATCHES)
        message(FATAL_ERROR "perception requires FlatBuffers 25.9.23; " "headers under '${PERCEPTION_FLATBUFFERS_INCLUDE_DIR}' are incompatible")
    endif()

    add_library(_perception_sdk INTERFACE)
    add_library(perception::sdk ALIAS _perception_sdk)

    target_compile_features(_perception_sdk INTERFACE cxx_std_20)
    target_include_directories(_perception_sdk INTERFACE "${_PERCEPTION_SDK_ROOT}/cpp" "${PERCEPTION_FLATBUFFERS_INCLUDE_DIR}")
endfunction()

function(perception_enable_python_bridge target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "perception_enable_python_bridge() target not found: ${target}")
    endif()

    if(NOT TARGET perception::sdk)
        perception_enable_sdk()
    endif()

    find_package(
        Python3 3.10
        COMPONENTS Development
        REQUIRED)

    set(_PERCEPTION_PYTHON_BRIDGE_SOURCE "${_PERCEPTION_SDK_ROOT}/cpp/python_bridge/perception_python_bridge.cpp")
    if(NOT EXISTS "${_PERCEPTION_PYTHON_BRIDGE_SOURCE}")
        message(FATAL_ERROR "perception Python bridge was not generated")
    endif()

    target_sources(${target} PRIVATE "${_PERCEPTION_PYTHON_BRIDGE_SOURCE}")
    target_link_libraries(${target} PRIVATE perception::sdk Python3::Python)
endfunction()
