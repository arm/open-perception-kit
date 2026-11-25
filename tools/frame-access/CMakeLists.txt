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
#
cmake_minimum_required(VERSION 3.22)
project(frame_access_contract_tests LANGUAGES CXX)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(FLATC flatc REQUIRED)
find_path(FLATBUFFERS_INCLUDE_DIR flatbuffers/flatbuffers.h REQUIRED)
set(GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_custom_command(
    OUTPUT "${GENERATED}/frame_reply_generated.h" "${GENERATED}/frame_reply_generated.rs" "${GENERATED}/frame_reply.bfbs"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/generate.py" --flatc "${FLATC}" --output "${GENERATED}"
    DEPENDS generate.py ../../schemas/frame-access/frame_reply.fbs ../../tools/perception/sdk.json
    VERBATIM)
add_executable(profile_test profile_test.cpp "${GENERATED}/frame_reply_generated.h")
target_include_directories(profile_test PRIVATE "${GENERATED}" "${FLATBUFFERS_INCLUDE_DIR}")
target_compile_features(profile_test PRIVATE cxx_std_17)
target_compile_options(profile_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
enable_testing()
add_test(NAME frame_access_profile COMMAND profile_test)
set_tests_properties(frame_access_profile PROPERTIES TIMEOUT 30)

option(NATIVE_TESTS "Build C++/Rust iceoryx2 interoperability tests" OFF)
if(NATIVE_TESTS)
    find_package(iceoryx2-cxx 0.10.0 EXACT REQUIRED)
    find_program(CARGO cargo REQUIRED)
    set(ICEORYX2_SOURCE_DIR
        ""
        CACHE PATH "Unmodified iceoryx2 0.10.0 source")
    if(NOT EXISTS "${ICEORYX2_SOURCE_DIR}/iceoryx2/Cargo.toml")
        message(FATAL_ERROR "ICEORYX2_SOURCE_DIR must contain the 0.10.0 Rust crate")
    endif()
    get_filename_component(OPK_ROOT "../.." ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    configure_file(native/Cargo.toml.in native/Cargo.toml @ONLY)
    add_executable(backend_test native/backend_test.cpp
        "${OPK_ROOT}/development/elements/opkcomm/frame_access_server.cpp"
        "${OPK_ROOT}/development/common/mediaio/Common.cpp")
    target_compile_features(backend_test PRIVATE cxx_std_20)
    target_compile_options(backend_test PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_include_directories(backend_test PRIVATE "${GENERATED}"
        "${OPK_ROOT}/generated/open_perception_kit/cpp"
        "${OPK_ROOT}/development/common" "${OPK_ROOT}/development/elements/opkcomm")
    target_link_libraries(backend_test PRIVATE iceoryx2-cxx::static-lib-cxx)
    add_dependencies(backend_test profile_test)
    add_test(NAME frame_access_backend COMMAND backend_test
        "${CMAKE_CURRENT_BINARY_DIR}/backend-runtime" "${GENERATED}/frame_reply.bfbs")
    set_tests_properties(frame_access_backend PROPERTIES TIMEOUT 20)
    add_executable(frame_access_server native/server.cpp)
    target_compile_features(frame_access_server PRIVATE cxx_std_20)
    target_compile_options(frame_access_server PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_include_directories(frame_access_server PRIVATE "${GENERATED}" "${OPK_ROOT}/generated/open_perception_kit/cpp" "${OPK_ROOT}/development/elements/opkcomm")
    target_link_libraries(frame_access_server PRIVATE iceoryx2-cxx::static-lib-cxx)
    add_dependencies(frame_access_server profile_test)
    add_custom_target(
        native_client ALL
        COMMAND "${CMAKE_COMMAND}" -E env "FRAME_ACCESS_GENERATED_DIR=${GENERATED}" "${CARGO}" build --manifest-path "${CMAKE_CURRENT_BINARY_DIR}/native/Cargo.toml" --target-dir "${CMAKE_CURRENT_BINARY_DIR}/cargo-target"
                --bin frame_access_client
        VERBATIM)
    add_dependencies(native_client profile_test)
    add_test(NAME frame_access_native COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/native/run.py" --server "$<TARGET_FILE:frame_access_server>" --client
                                              "${CMAKE_CURRENT_BINARY_DIR}/cargo-target/debug/frame_access_client" --schema "${GENERATED}/frame_reply.bfbs" --runtime-root "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(frame_access_native PROPERTIES TIMEOUT 90)
endif()
