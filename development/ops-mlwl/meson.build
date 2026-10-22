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

mlwl_enabled = false
mlwl_option = get_option('mlwl')
if mlwl_option.disabled()
  subdir_done()
endif

mlwl_dep = dependency('MLWorkloadLibraryForVulkan', version : '==0.11.0',
  method : 'cmake', modules : ['MLWorkloadLibraryForVulkan::mlworkloadlib_shared'],
  required : mlwl_option)
if not mlwl_dep.found()
  subdir_done()
endif

# Match the public binary release's Vulkan-Hpp version and dispatcher layout.
mlwl_vulkan_dep = dependency('VulkanHeaders', version : '==1.4.357',
  method : 'cmake', modules : ['Vulkan::Headers'], required : mlwl_option)
if not mlwl_vulkan_dep.found()
  subdir_done()
endif
if release_package
  error('MLWL is not supported in release packages yet; use -Dmlwl=disabled or -Drelease_package=false.')
endif
mlwl_enabled = true
mlwl_ops = shared_module(
  'opk-mlwl-ops',
  sources : ['InferenceOp.cpp', 'Plugin.cpp'],
  include_directories : common_include,
  dependencies : [opk_deps, mlwl_dep, mlwl_vulkan_dep],
  link_with : [common],
  override_options : ['b_ndebug=true'],
  name_prefix : '',
  gnu_symbol_visibility : 'default',
  install : release_package,
  install_dir : 'lib/opk',
  install_rpath : '$ORIGIN',
)
