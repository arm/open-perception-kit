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


asio_dep = dependency('asio', required: true,
                             fallback: ['asio', 'asio_dep'])
websocketpp_dep = dependency('websocketpp', required: true,
                             fallback: ['websocketpp', 'websocketpp_dep'])
openssl_dep = dependency('openssl', required : true)

opksink_cpp_args = []
if get_option('tests') or get_option('debug')
  opksink_cpp_args += ['-DOPK_ENABLE_FACTORY_FAILURE_INJECTION']
endif

opkinferpre = shared_module(
  'opksink',                   
  sources: [
    'opksink.cpp',
    'http_server.cpp',
    'webrtc_session.cpp',
    'webrtc_ws.cpp',
    'ctrl_ws.cpp',
    'utils.cpp',
    'model_reg.cpp'
  ],
  cpp_args: opksink_cpp_args,
  include_directories: [common_include],
  dependencies: [opk_deps, asio_dep, websocketpp_dep, openssl_dep], 
  gnu_symbol_visibility: 'hidden', 
  link_with: [ common ],
  install: release_package,
  install_dir: 'lib/gstreamer-1.0',
  install_rpath: '$ORIGIN/../opk',
)

opksink_factory_failure_test = executable(
  'opksink_factory_failure_test',
  ['../../tests/opksink_factory_failure_test.cpp'],
  dependencies : [gst_dep, glib_dep],
  build_by_default : get_option('tests'),
  install : false,
)
