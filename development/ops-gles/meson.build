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


glesops = []
gles_option = get_option('gpu_preprocessing').enable_auto_if(host_machine.system() == 'linux')
if host_machine.system() == 'linux' and not gles_option.disabled()
  egl_dep = dependency('egl', required: gles_option)
  gles_dep = dependency('glesv2', required: gles_option)
  if egl_dep.found() and gles_dep.found()
    shader_configuration = configuration_data()
    shader_configuration.set('PREPROCESS_SHADER_BODY', fs.read('preprocess.comp'))
    configure_file(
      input: 'PreprocessShader.h.in',
      output: 'PreprocessShader.h',
      configuration: shader_configuration,
    )
    glesops = shared_module(
      'opk-gpu-ops',
      sources: ['ImagePreprocessOp.cpp', 'GpuPreprocess.cpp'],
      include_directories: common_include,
      dependencies: [opk_deps, egl_dep, gles_dep],
      link_with: common,
      name_prefix: '',
      gnu_symbol_visibility: 'default',
      install: release_package,
      install_dir: get_option('libdir') / 'opk',
      install_rpath: '$ORIGIN',
    )
  endif
elif gles_option.enabled()
  error('GLES preprocessing requires Linux. Use CPU preprocessing on this platform.')
endif
