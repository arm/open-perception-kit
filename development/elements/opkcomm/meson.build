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


asio_dep = dependency('asio', required: true,
                      fallback: ['asio', 'asio_dep'])
websocketpp_dep = dependency('websocketpp', required: true,
                             fallback: ['websocketpp', 'websocketpp_dep'])

opkcomm_frame_access_enabled = false
frame_sources = []
frame_dependencies = []
frame_cpp_args = []
if not get_option('opkcomm_iceoryx2').disabled()
  native_dep = dependency('iceoryx2-cxx', version: '==0.10.0', method: 'cmake',
                          modules: ['iceoryx2-cxx::static-lib-cxx'],
                          required: get_option('opkcomm_iceoryx2'))
  if native_dep.found()
    opkcomm_frame_access_enabled = true
    repo_root = meson.project_source_root() / '..'
    frame_contract = custom_target('frame_access_contract',
      input: repo_root / 'schemas/frame-access/frame_reply.fbs',
      output: ['frame_reply_generated.h', 'frame_reply_generated.rs', 'frame_reply.bfbs'],
      command: [find_program('python3'), repo_root / 'tools/frame-access/generate.py',
                '--flatc', find_program('flatc'), '--output', meson.current_build_dir()],
      depend_files: [repo_root / 'tools/frame-access/generate.py', repo_root / 'tools/perception/sdk.json'],
      install: release_package,
      install_dir: [false, false, get_option('datadir') / 'opk/frame-access'])
    schema_path = release_package ? '' : meson.current_build_dir() / 'frame_reply.bfbs'
    frame_sources = [frame_contract[0], 'frame_access_server.cpp']
    frame_dependencies = [native_dep]
    frame_cpp_args = ['-DOPK_FRAME_ACCESS', '-DOPK_FRAME_SCHEMA_PATH="' + schema_path + '"']
  endif
endif

opkcomm = shared_module(
  'opkcomm',                   
  sources: [
    'opkcomm.cpp',
    'writer.cpp',
    'file_writer.cpp',
    'tcp_writer.cpp',
    'websocket_writer.cpp',
  ] + frame_sources,
  cpp_args: frame_cpp_args,
  include_directories: [common_include],
  dependencies: [opk_deps, dependency('zlib'), asio_dep, websocketpp_dep] + frame_dependencies,
  gnu_symbol_visibility: 'hidden', 
  link_with: [ common ],
  install: release_package,
  install_dir: get_option('libdir') / 'gstreamer-1.0',
  install_rpath: '$ORIGIN/../opk',
)
