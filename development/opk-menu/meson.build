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


opk_menu = executable(
  'opk-menu',
  sources: ['opk-menu.cpp', 'menu.cpp', 'discover.cpp'],
  # build.sh stages this executable beside its Runtime/common libraries in the
  # repository's tools directory.
  build_rpath: '$ORIGIN/',
  include_directories: [common_include, runtime_include],
  install: false,
  dependencies: [fmt_dep, magic_enum_dep, nlohmann_dep, tl_expected_dep, open_perception_kit_dep],
  link_with: [opk_runtime, common],
)
