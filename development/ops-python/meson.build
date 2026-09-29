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

python_embed_dep = opk_python.dependency(embed : true, required : true)
python_runtime_path = ''
if not release_package
  python_runtime_path = meson.project_source_root() / '../generated/open_perception_kit/python/src'
endif

python_ops_sources = [
  'PythonRuntime.cpp',
  'PythonScriptOp.cpp',
  'TensorBridge.cpp',
]

python_executable = opk_python.full_path()
if release_package
  python_executable = 'python3.13'
endif

python_ops_cpp_args = [
  '-DOPK_PYTHON_EXECUTABLE="' + python_executable + '"',
  '-DOPK_DEVELOPMENT_PYTHON_PATH="' + python_runtime_path + '"',
]

python_ops = shared_module(
  'opk-python-ops',
  sources : python_ops_sources + ['Plugin.cpp'],
  include_directories : [common_include, include_directories(opk_numpy_include)],
  dependencies : [opk_deps, open_perception_kit_python_bridge_dep, python_embed_dep],
  link_with : [common],
  cpp_args : python_ops_cpp_args,
  name_prefix : '',
  gnu_symbol_visibility : 'default',
  install : release_package,
  install_dir : 'lib/opk',
  install_rpath : '$ORIGIN',
)

python_ops_test_dep = declare_dependency(
  include_directories : [include_directories('.'), include_directories(opk_numpy_include)],
  dependencies : [opk_deps, open_perception_kit_python_bridge_dep, python_embed_dep],
  link_with : [common],
  sources : python_ops_sources,
  compile_args : python_ops_cpp_args,
)

if release_package
  install_data(
    'opk_python_ops.pyi',
    install_dir : 'share/opk/python',
  )
endif
