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
# Provides the pre-generated open_perception_kit C++ SDK as a Meson dependency.
# Schema changes do NOT trigger regeneration during the build. Re-run your
# project SDK generation script before configuring or rebuilding consumers.
#
# Usage:
#   subdir('open_perception_kit')
#   executable('app', 'app.cpp', dependencies: [open_perception_kit_dep])
#
# Embedded Python bridge usage, when generated:
#   subdir('open_perception_kit/python_bridge')
#   executable('host', 'host.cpp', dependencies: [open_perception_kit_python_bridge_dep])

_open_perception_kit_cpp = meson.get_compiler('cpp')
open_perception_kit_version = '0.1.1'
open_perception_kit_flatbuffers_version_requirement = '==25.9.23'
open_perception_kit_python_version_requirement = '>=3.10'
open_perception_kit_python_bridge_available = true
open_perception_kit_python_bridge_module = 'open_perception_kit_bridge'

# ── FlatBuffers ───────────────────────────────────────────────────────────
# partial_dependency strips all link/rpath info — only include dirs and
# compile args propagate to consumers. FlatBuffers is header-only in this
# integration.
_open_perception_kit_flatbuffers_dep_full = dependency(
  'flatbuffers',
  required: false,
  version: open_perception_kit_flatbuffers_version_requirement,
)
_open_perception_kit_extra_deps = []
if _open_perception_kit_flatbuffers_dep_full.found()
  _open_perception_kit_extra_deps += [
    _open_perception_kit_flatbuffers_dep_full.partial_dependency(
      compile_args : true,
      includes     : true,
    )
  ]
else
  _open_perception_kit_flatbuffers_version_check = '''
    #include <flatbuffers/base.h>
    static_assert(FLATBUFFERS_VERSION_MAJOR == 25);
    static_assert(FLATBUFFERS_VERSION_MINOR == 9);
    static_assert(FLATBUFFERS_VERSION_REVISION == 23);
    int main() { return 0; }
  '''
  if not _open_perception_kit_cpp.compiles(
    _open_perception_kit_flatbuffers_version_check,
    name : 'open_perception_kit FlatBuffers 25.9.23 compatibility',
  )
    error('open_perception_kit requires FlatBuffers 25.9.23 headers')
  endif
endif

# ── C++20 requirement ─────────────────────────────────────────────────────
_open_perception_kit_cpp20_args = _open_perception_kit_cpp.get_supported_arguments('-std=c++20')
if _open_perception_kit_cpp20_args.length() == 0
  _open_perception_kit_cpp20_args = _open_perception_kit_cpp.get_supported_arguments('/std:c++20')
endif
if _open_perception_kit_cpp20_args.length() == 0
  warning('open_perception_kit: compiler does not accept -std=c++20 or /std:c++20; '
          + 'ensure your project sets cpp_std >= c++20 in default_options')
endif

# ── Public dependency ─────────────────────────────────────────────────────
# Generated headers live under the pre-generated SDK root.
_open_perception_kit_inc = include_directories('../..')

open_perception_kit_dep = declare_dependency(
  include_directories : [_open_perception_kit_inc],
  compile_args        : _open_perception_kit_cpp20_args,
  dependencies        : _open_perception_kit_extra_deps,
)
