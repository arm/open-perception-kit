# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# Generated file. Do not edit.
# SDK users: change schemas or generator inputs, then regenerate this file.
#
# This file exposes a pre-generated open_perception_kit C++ SDK to CMake consumers.
# Schema changes do NOT trigger regeneration during the build. Re-run your
# project SDK generation script before configuring or rebuilding consumers.
#
# Usage in your CMakeLists.txt:
#
#   include(path/to/generated/cpp/cmake/open_perception_kit.cmake)
#   open_perception_kit_enable_sdk()
#   target_link_libraries(my_app PRIVATE open_perception_kit::sdk)
#
# Overridable variables (set BEFORE including this file):
#   OPEN_PERCEPTION_KIT_FLATBUFFERS_INCLUDE_DIR - FlatBuffers headers (auto-discovered)
#
get_filename_component(_OPEN_PERCEPTION_KIT_SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(OPEN_PERCEPTION_KIT_VERSION "0.1.1")
set(OPEN_PERCEPTION_KIT_FLATBUFFERS_VERSION_REQUIREMENT "==25.9.23")
set(OPEN_PERCEPTION_KIT_PYTHON_VERSION_REQUIREMENT ">=3.10")
set(OPEN_PERCEPTION_KIT_PYTHON_BRIDGE_AVAILABLE TRUE)
set(OPEN_PERCEPTION_KIT_PYTHON_BRIDGE_MODULE "open_perception_kit_bridge")

function(open_perception_kit_enable_sdk)
    if(TARGET open_perception_kit::sdk)
        message(FATAL_ERROR "open_perception_kit_enable_sdk() has already been called")
    endif()

    if(NOT DEFINED OPEN_PERCEPTION_KIT_FLATBUFFERS_INCLUDE_DIR OR OPEN_PERCEPTION_KIT_FLATBUFFERS_INCLUDE_DIR STREQUAL "")
        find_path(OPEN_PERCEPTION_KIT_FLATBUFFERS_INCLUDE_DIR flatbuffers/flatbuffers.h REQUIRED)
    endif()

    include(CheckCXXSourceCompiles)
    set(_OPEN_PERCEPTION_KIT_SAVED_REQUIRED_INCLUDES "${CMAKE_REQUIRED_INCLUDES}")
    set(CMAKE_REQUIRED_INCLUDES "${OPEN_PERCEPTION_KIT_FLATBUFFERS_INCLUDE_DIR}")
    check_cxx_source_compiles(
        "#include <flatbuffers/base.h>
     static_assert(FLATBUFFERS_VERSION_MAJOR == 25);
     static_assert(FLATBUFFERS_VERSION_MINOR == 9);
     static_assert(FLATBUFFERS_VERSION_REVISION == 23);
     int main() { return 0; }"
        _OPEN_PERCEPTION_KIT_FLATBUFFERS_25_9_23_MATCHES)
    set(CMAKE_REQUIRED_INCLUDES "${_OPEN_PERCEPTION_KIT_SAVED_REQUIRED_INCLUDES}")
    if(NOT _OPEN_PERCEPTION_KIT_FLATBUFFERS_25_9_23_MATCHES)
        message(FATAL_ERROR "open_perception_kit requires FlatBuffers 25.9.23; " "headers under '${OPEN_PERCEPTION_KIT_FLATBUFFERS_INCLUDE_DIR}' are incompatible")
    endif()

    add_library(_open_perception_kit_sdk INTERFACE)
    add_library(open_perception_kit::sdk ALIAS _open_perception_kit_sdk)

    target_compile_features(_open_perception_kit_sdk INTERFACE cxx_std_20)
    target_include_directories(_open_perception_kit_sdk INTERFACE "${_OPEN_PERCEPTION_KIT_SDK_ROOT}/cpp" "${OPEN_PERCEPTION_KIT_FLATBUFFERS_INCLUDE_DIR}")
endfunction()

function(open_perception_kit_enable_python_bridge target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "open_perception_kit_enable_python_bridge() target not found: ${target}")
    endif()

    if(NOT TARGET open_perception_kit::sdk)
        open_perception_kit_enable_sdk()
    endif()

    find_package(
        Python3 3.10
        COMPONENTS Development
        REQUIRED)

    set(_OPEN_PERCEPTION_KIT_PYTHON_BRIDGE_SOURCE "${_OPEN_PERCEPTION_KIT_SDK_ROOT}/cpp/python_bridge/open_perception_kit_python_bridge.cpp")
    if(NOT EXISTS "${_OPEN_PERCEPTION_KIT_PYTHON_BRIDGE_SOURCE}")
        message(FATAL_ERROR "open_perception_kit Python bridge was not generated")
    endif()

    target_sources(${target} PRIVATE "${_OPEN_PERCEPTION_KIT_PYTHON_BRIDGE_SOURCE}")
    target_link_libraries(${target} PRIVATE open_perception_kit::sdk Python3::Python)
endfunction()
