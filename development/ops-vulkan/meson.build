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

gpu_option = get_option('gpu_preprocessing')
gpu_enabled = gpu_option.enabled() or (gpu_option.auto() and host_machine.system() == 'linux')
if gpu_enabled
  vulkan_dep = dependency('vulkan', version: '>=1.1', required: true)
  shader_compiler = find_program('glslangValidator', required: true)
  shader_header = custom_target(
    'preprocess-spirv',
    input: 'preprocess.comp',
    output: 'preprocess_spirv.h',
    command: [shader_compiler, '-V', '--target-env', 'vulkan1.1', '--vn',
              'preprocessingSpirv', '-o', '@OUTPUT@', '@INPUT@'],
  )
  gpu_sources = ['GpuPreprocess.cpp', 'ImagePreprocessOp.cpp']
  if host_machine.system() == 'linux'
    gpu_sources += 'LinuxImport.cpp'
  endif
  gpuops = shared_module(
    'opk-gpu-ops',
    sources: [gpu_sources, shader_header],
    include_directories: common_include,
    dependencies: opk_deps + [vulkan_dep],
    link_with: [common],
    name_prefix: '',
    gnu_symbol_visibility: 'default',
    install: release_package,
    install_dir: get_option('libdir') / 'opk',
    install_rpath: '$ORIGIN',
  )
endif
