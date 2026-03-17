gstamptracker = shared_module(
  'amptracker',
  sources: [
    'amptracker.cpp',
    'Tracker.cpp',
    'TrackState.cpp',
    'Utils.cpp',
    'FrameInputs.cpp',
    'Matching.cpp',
    'TrackLifecycle.cpp',
    'TrackingOutput.cpp'
  ],
  include_directories: common_include,
  dependencies: amp_deps,
  cpp_args: ['-DFMT_HEADER_ONLY'],
  link_with: [ common ],
  gnu_symbol_visibility: 'hidden',
  install: false
)
