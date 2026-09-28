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

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES OPK_EXECUTORCH_X86_64_C_COMPILER OPK_EXECUTORCH_X86_64_CXX_COMPILER OPK_EXECUTORCH_X86_64_AR OPK_EXECUTORCH_X86_64_RANLIB OPK_EXECUTORCH_X86_64_STRIP)

foreach(tool IN ITEMS C_COMPILER CXX_COMPILER AR RANLIB STRIP)
    if(NOT DEFINED OPK_EXECUTORCH_X86_64_${tool} OR OPK_EXECUTORCH_X86_64_${tool} STREQUAL "")
        message(FATAL_ERROR "OPK_EXECUTORCH_X86_64_${tool} is required")
    endif()
endforeach()

set(CMAKE_C_COMPILER
    "${OPK_EXECUTORCH_X86_64_C_COMPILER}"
    CACHE FILEPATH "x86_64 C compiler")
set(CMAKE_CXX_COMPILER
    "${OPK_EXECUTORCH_X86_64_CXX_COMPILER}"
    CACHE FILEPATH "x86_64 C++ compiler")
set(CMAKE_ASM_COMPILER
    "${OPK_EXECUTORCH_X86_64_C_COMPILER}"
    CACHE FILEPATH "x86_64 assembler")
set(CMAKE_AR
    "${OPK_EXECUTORCH_X86_64_AR}"
    CACHE FILEPATH "x86_64 archiver")
set(CMAKE_RANLIB
    "${OPK_EXECUTORCH_X86_64_RANLIB}"
    CACHE FILEPATH "x86_64 ranlib")
set(CMAKE_STRIP
    "${OPK_EXECUTORCH_X86_64_STRIP}"
    CACHE FILEPATH "x86_64 strip tool")

set(CMAKE_C_FLAGS_INIT "-m64")
set(CMAKE_CXX_FLAGS_INIT "-m64")
set(CMAKE_ASM_FLAGS_INIT "-m64")

if(EXISTS "/usr/x86_64-linux-gnu")
    set(CMAKE_FIND_ROOT_PATH "/usr/x86_64-linux-gnu")
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
endif()
