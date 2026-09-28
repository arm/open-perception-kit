# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates


asio_dep = dependency('asio', required: true,
                      fallback: ['asio', 'asio_dep'])
websocketpp_dep = dependency('websocketpp', required: true,
                             fallback: ['websocketpp', 'websocketpp_dep'])

opkcomm = shared_module(
  'opkcomm',                   
  sources: [
    'opkcomm.cpp',
    'writer.cpp',
    'file_writer.cpp',
    'tcp_writer.cpp',
    'websocket_writer.cpp',
  ],
  include_directories: [common_include],
  dependencies: [opk_deps, dependency('zlib'), asio_dep, websocketpp_dep], 
  gnu_symbol_visibility: 'hidden', 
  link_with: [ common ],
  install: release_package,
  install_dir: 'lib/gstreamer-1.0',
  install_rpath: '$ORIGIN/../opk',
)
