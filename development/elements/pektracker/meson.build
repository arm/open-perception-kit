gstpektracker = shared_module(
  'pektracker',
  sources: [
    'pektracker.cpp',
    'Tracker.cpp',
    'TrackState.cpp',
    'Utils.cpp',
    'FrameInputs.cpp',
    'Matching.cpp',
    'TrackLifecycle.cpp',
    'TrackingOutput.cpp'
  ],
  include_directories: common_include,
  dependencies: pek_deps,
  cpp_args: ['-DFMT_HEADER_ONLY'],
  link_with: [ common ],
  gnu_symbol_visibility: 'hidden',
  install: false
)
