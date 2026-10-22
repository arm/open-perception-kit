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

if deb_package
if is_disabler(executorchops)
  error('deb_package requires -Dexecutorch=enabled and a complete ExecuTorch SDK.')
endif
if not python_ops_enabled
  error('deb_package requires -Dpython_ops=enabled and the locked Python Ops runtime.')
endif

deb_revision = get_option('deb_package_revision')
if deb_revision == ''
  error('deb_package_revision must not be empty.')
endif

if host_machine.cpu_family() == 'x86_64'
  deb_architecture = 'amd64'
  deb_multiarch = 'x86_64-linux-gnu'
elif host_machine.cpu_family() == 'aarch64'
  deb_architecture = 'arm64'
  deb_multiarch = 'aarch64-linux-gnu'
else
  error('opk-runtime Debian packages support only x86_64 and aarch64.')
endif

package_script = find_program(
  opk_repository_root / 'scripts/private/packaging/package-opk-runtime-deb.sh',
)
meson_program = find_program('meson')
optional_modules = []
if gpu_enabled
  optional_modules += gpuops
endif

mlwl_package_args = []
if mlwl_enabled
  mlwl_package_dir = run_command('sh', '-c', 'printf %s "$MLWorkloadLibraryForVulkan_DIR"', check : true).stdout().strip()
  if mlwl_package_dir == ''
    error('MLWL packaging requires MLWorkloadLibraryForVulkan_DIR from install-mlwl.py.')
  endif
  mlwl_package_args = ['--mlwl-prefix', mlwl_package_dir / '../../..']
  optional_modules += mlwl_ops
endif

opk_runtime_deb = custom_target(
  'opk-runtime-deb',
  output : '@0@_@1@-@2@_@3@.deb'.format(
    'opk-runtime',
    meson.project_version(),
    deb_revision,
    deb_architecture,
  ),
  command : [
    package_script,
    '--build-dir', meson.project_build_root(),
    '--meson', meson_program,
    '--repo-root', opk_repository_root,
    '--installed-prefix', get_option('prefix'),
    '--installed-libdir', get_option('libdir'),
    '--python-runtime', opk_python.full_path(),
    '--onnx-runtime', onnx_libdir / onnx_library,
    '--onnx-license-dir', onnx_root / 'share/doc/onnxruntime',
    '--executorch-license-dir', executorch_root / '..' / 'executorch-legal-documentation',
    '--flatbuffers-schema-dir', opk_repository_root / 'schemas/perception/metadata',
    '--version', meson.project_version(),
    '--revision', deb_revision,
    '--architecture', deb_architecture,
    '--multiarch', deb_multiarch,
    '--output', '@OUTPUT@',
  ] + mlwl_package_args,
  depends : [
    common,
    opk_runtime,
    stdops,
    onnxops,
    executorchops,
    python_ops,
    gstopkinfer,
    gstopktracker,
    gstopkperformance,
    gstopkosd,
    opkinferpre,
    opksrc,
    opkcomm,
  ] + optional_modules,
  build_by_default : false,
  build_always_stale : true,
  console : true,
)
endif

if demo_deb_package
if host_machine.cpu_family() == 'x86_64'
  demo_deb_architecture = 'amd64'
elif host_machine.cpu_family() == 'aarch64'
  demo_deb_architecture = 'arm64'
else
  error('opk-demo Debian packages support only x86_64 and aarch64.')
endif

demo_package_script = find_program(
  opk_repository_root / 'scripts/private/packaging/package-opk-demo-deb.sh',
)
demo_meson_program = find_program('meson')

opk_demo_deb = custom_target(
  'opk-demo-deb',
  output : '@0@_@1@-@2@_@3@.deb'.format(
    'opk-demo',
    meson.project_version(),
    get_option('deb_package_revision'),
    demo_deb_architecture,
  ),
  command : [
    demo_package_script,
    '--build-dir', meson.project_build_root(),
    '--meson', demo_meson_program,
    '--repo-root', opk_repository_root,
    '--installed-prefix', get_option('prefix'),
    '--version', meson.project_version(),
    '--revision', get_option('deb_package_revision'),
    '--architecture', demo_deb_architecture,
    '--output', '@OUTPUT@',
  ],
  depends : [opk_menu],
  build_by_default : false,
  build_always_stale : true,
  console : true,
)
endif
