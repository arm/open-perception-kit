# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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

cmake_minimum_required(VERSION 3.20)
project(mobilesam_prompts LANGUAGES CXX)
set(OPEN_PERCEPTION_KIT_SDK
    "${CMAKE_CURRENT_LIST_DIR}/../../../generated/open_perception_kit"
    CACHE PATH "Generated Open Perception Kit SDK directory")
include("${OPEN_PERCEPTION_KIT_SDK}/cpp/cmake/open_perception_kit.cmake")
open_perception_kit_enable_sdk()
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0)
add_executable(mobilesam-prompts main.cpp)
target_compile_features(mobilesam-prompts PRIVATE cxx_std_20)
target_link_libraries(mobilesam-prompts PRIVATE open_perception_kit::sdk PkgConfig::GSTREAMER)
