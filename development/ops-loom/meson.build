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

if not get_option('loom')
  subdir_done()
endif
loom_ops = shared_module(
  'opk-loom-ops',
  sources : ['Inference.cpp', 'InferenceOp.cpp', 'Plugin.cpp'],
  include_directories : common_include,
  dependencies : [opk_deps, meson.get_compiler('cpp').find_library('dl')],
  link_with : [common],
  name_prefix : '',
  gnu_symbol_visibility : 'default',
  install : release_package,
  install_dir : get_option('libdir') / 'opk',
  install_rpath : '$ORIGIN',
)
