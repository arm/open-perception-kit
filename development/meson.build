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

project('opk_elements', ['c', 'cpp'],
    version: '0.1.1',
  default_options: [
    'c_std=c11',
    'cpp_std=c++20',
    'warning_level=2',
    'b_lto=false', # true for release
  ]
)

# The Meson source tree is development/, while repository-owned dependencies,
# configuration, metadata, scripts, and tests live one directory above it.
# Keep this derived from Meson's source root so native builds do not depend on
# the checkout being mounted at a particular filesystem location.
opk_repository_root = meson.project_source_root() / '..'

add_project_arguments('-Wno-unused-variable', language: ['c', 'cpp'])
add_project_arguments('-Wno-unused-parameter', language: ['c', 'cpp'])
add_project_arguments('-Wno-unused-function', language: ['c', 'cpp'])
add_project_arguments('-Wno-unused-but-set-variable', language: ['c', 'cpp'])
add_project_arguments('-DFMT_HEADER_ONLY', language: 'cpp')

release_package = get_option('release_package')
deb_package = get_option('deb_package')
demo_deb_package = get_option('demo_deb_package')

if deb_package and not release_package
  error('deb_package requires -Drelease_package=true so Meson can stage install-RPATH binaries.')
endif

# -------- Core deps (always)
stb_dep = subproject('stb').get_variable('stb_dep')
gst_dep      = dependency('gstreamer-1.0',        required: true)
gstbase_dep  = dependency('gstreamer-base-1.0',   required: true)
gstv_dep     = dependency('gstreamer-video-1.0',  required: true)
gsta_dep     = dependency('gstreamer-audio-1.0',  required: true)
gstalloc_dep = dependency('gstreamer-allocators-1.0', required: true)

glib_dep     = dependency('glib-2.0',             required: true)
gobject_dep  = dependency('gobject-2.0',          required: true)

# -------- Web deps (Web output ALWAYS ON)
# Require WebRTC + SDP (browser playback via WebRTC is mandatory)
webrtc_dep   = dependency('gstreamer-webrtc-1.0', required: true)
sdp_dep      = dependency('gstreamer-sdp-1.0',    required: true)

# Try to enable embedded HTTP/WS server (optional)
# Prefer libsoup-3.0; fall back to 2.4. Also need json-glib.
soup_dep = dependency('libsoup-3.0', required: false)
use_soup2 = false
if not soup_dep.found()
  soup_dep = dependency('libsoup-2.4', required: false)
  use_soup2 = soup_dep.found()
endif
json_dep = dependency('json-glib-1.0', required: false)

fmt_dep = dependency('fmt',
  version: ['>=12.0.0', '<13.0.0'],
  fallback : ['fmt', 'fmt_dep'],
  required : true)
threads_dep = dependency('threads')

nlohmann_dep = dependency('nlohmann_json',
                      required : true,
                      fallback : ['nlohmann_json', 'nlohmann_json_dep'])

jsoncons_dep = dependency(
  'jsoncons',
  required : true,
  fallback : ['jsoncons', 'jsoncons_dep'],
)

magic_enum_dep = dependency(
  'magic_enum',
  required : true,
  fallback : ['magic_enum', 'magic_enum_dep']
)

httplib_dep = dependency(
  'cpp-httplib',
  version : '>=0.56.0',
  required : true,
  fallback : ['cpp-httplib', 'cpp_httplib_dep']
)

tl_expected_dep = dependency('tl-expected', required : true)

web_server_enabled = soup_dep.found() and json_dep.found()

if release_package and not web_server_enabled
  error('Release packages require libsoup and json-glib for HTTP-enabled opksink.')
endif

# -------- ONNX
fs = import('fs')

onnx_root = ''
onnx_candidates = []

onnx_root_env = run_command('sh', '-c', 'printf %s "$OPK_ONNXRUNTIME_ROOT"', check: false).stdout().strip()
if onnx_root_env != ''
  onnx_candidates += [onnx_root_env]
endif

onnx_root_cross = meson.get_external_property('opk_onnxruntime_root', '')
if onnx_root_cross != ''
  onnx_candidates += [onnx_root_cross]
endif

onnx_candidates += [
  '/opt/opk-deps/onnxruntime',
  opk_repository_root / 'deps/onnxruntime',
  opk_repository_root / 'onnxruntime-linux-aarch64-1.24.4',
]

foreach candidate : onnx_candidates
  if fs.is_dir(candidate / 'include') and fs.is_dir(candidate / 'lib')
    onnx_root = candidate
    break
  endif
endforeach

if onnx_root == ''
  error('ONNX Runtime not found. Set OPK_ONNXRUNTIME_ROOT or provide include/ and lib/ under one of: ' + ', '.join(onnx_candidates))
endif

message('Using ONNX Runtime from: ' + onnx_root)

onnx_libdir  = onnx_root / 'lib'
onnx_dep = declare_dependency(
  compile_args: ['-I' + (onnx_root / 'include')],
  link_args: ['-L' + onnx_libdir, '-lonnxruntime', '-Wl,--no-undefined']
)

if release_package
  if host_machine.cpu_family() != 'x86_64' and host_machine.cpu_family() != 'aarch64'
    error('Release packages support only x86_64 and aarch64.')
  endif
endif

# -------- Exported defs + deps for subdirs
opk_defs = ['-DOPK_WEB_MODE=1']  # Web mode always compiled
if web_server_enabled
  opk_defs += ['-DOPK_WEB_SERVER=1']
  if use_soup2
    opk_defs += ['-DUSE_SOUP2=1']
  else
    opk_defs += ['-DUSE_SOUP3=1']
  endif
else
  opk_defs += ['-DOPK_WEB_SERVER=0']  # fallback to file-based/manual signaling
  warning('libsoup and/or json-glib not found — building opksink without embedded HTTP/WS server (manual signaling mode).')
endif
add_project_arguments(opk_defs, language: 'cpp')

opk_deps = [
  stb_dep,
  gst_dep, gstbase_dep, gstv_dep, gsta_dep, gstalloc_dep,
  glib_dep, gobject_dep,
  webrtc_dep, sdp_dep,
  fmt_dep, nlohmann_dep, nlohmann_dep, magic_enum_dep, tl_expected_dep, httplib_dep
]

if web_server_enabled
  opk_deps += [ soup_dep, json_dep ]
endif

# Common include(s)
common_include = include_directories('common', 'common/logging/include')

python_ops_option = get_option('python_ops')
python_ops_enabled = false
opk_python = disabler()
opk_numpy_include = ''
test_python = disabler()

if get_option('tests')
  test_python_path = run_command(
    'sh',
    '-c',
    'if [ -n "$OPK_DEVTOOLS_VENV" ]; then printf %s "$OPK_DEVTOOLS_VENV/bin/python"; fi',
    check : false,
  ).stdout().strip()
  if test_python_path != ''
    test_python = import('python').find_installation(test_python_path)
  else
    test_python = import('python').find_installation()
  endif
endif

if get_option('tests') or not python_ops_option.disabled()
  opk_python_path = run_command(
    'sh',
    '-c',
    'if [ -n "$OPK_PYTHON_RUNTIME_VENV" ]; then printf %s "$OPK_PYTHON_RUNTIME_VENV/bin/python"; elif [ -n "$OPK_DEVTOOLS_VENV" ]; then printf %s "$OPK_DEVTOOLS_VENV/bin/python"; fi',
    check : false,
  ).stdout().strip()
  if opk_python_path != ''
    opk_python = import('python').find_installation(opk_python_path, required : false)
  else
    opk_python = import('python').find_installation(required : false)
  endif

  if opk_python.found()
    numpy_probe = run_command(
      opk_python,
      ['-c', 'import numpy; print(numpy.get_include())'],
      check : false,
    )
    if numpy_probe.returncode() == 0
      opk_numpy_include = numpy_probe.stdout().strip()
      python_ops_enabled = not python_ops_option.disabled()
    elif python_ops_option.enabled()
      error('python_ops requires NumPy for the selected Python installation.')
    endif
  elif get_option('tests') or python_ops_option.enabled()
    error('A Python development installation is required.')
  endif
endif

subdir('perception')
opk_deps += [open_perception_kit_dep]

# gtest dependencies
cmake = import('cmake')

opt = cmake.subproject_options()
opt.add_cmake_defines({
  'BUILD_GMOCK': false,
  'INSTALL_GTEST': false,
})

gtest_cmake = cmake.subproject('gtest', options : opt)

gtest_dep      = gtest_cmake.dependency('gtest')
gtest_main_dep = gtest_cmake.dependency('gtest_main')

# Subdirs
subdir('common/logging')
subdir('config-validator')
subdir('common')
subdir('runtime')
subdir('elements/opkinfer')
subdir('ops-onnx')
subdir('ops-std')
if python_ops_enabled
  subdir('ops-python')
endif
subdir('elements/opktracker')
subdir('elements/opkperformance')
subdir('elements/opkosd')
subdir('elements/opksink')
subdir('elements/opkcomm')
subdir('opk-menu')
subdir('examples')

# Optional subdirs decision tree is inside the subdir
executorchops = disabler()
subdir('ops-executorch')

if deb_package or demo_deb_package
  subdir('packaging')
endif

if release_package
  install_subdir(
    'web/content',
    install_dir : 'web/content',
    strip_directory : true,
    exclude_files : [
      'package.json',
      'osd-renderer.test.mjs',
      'webrtc_client.test.mjs',
      'webrtc_config.test.mjs',
    ],
  )
endif

if get_option('tests')
  subdir('packaging/tests')
  subdir('tests')
endif

subdir('quality')
