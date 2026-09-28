# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates

gstopktracker = shared_module(
  'opktracker',
  sources: [
    'opktracker.cpp',
    'Tracker.cpp',
    'TrackState.cpp',
    'Utils.cpp',
    'FrameInputs.cpp',
    'Matching.cpp',
    'TrackLifecycle.cpp',
    'TrackingOutput.cpp'
  ],
  include_directories: common_include,
  dependencies: opk_deps,
  link_with: [ common ],
  gnu_symbol_visibility: 'hidden',
  install: release_package,
  install_dir: 'lib/gstreamer-1.0',
  install_rpath: '$ORIGIN/../opk',
)
