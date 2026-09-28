# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates


gstopkperformance = shared_module(
  'opkperformance',
  sources: [
    'opkperformance.cpp'
  ],
  include_directories: common_include,
  dependencies: opk_deps,
  link_with: [ common ],
  gnu_symbol_visibility: 'hidden',
  install: release_package,
  install_dir: 'lib/gstreamer-1.0',
  install_rpath: '$ORIGIN/../opk',
)
