runtime_include = include_directories('..')

opk_runtime = shared_library(
  'opk-runtime',
  sources: [
    'Logging.cpp',
    'OpChain.cpp',
    'PerformanceMetrics.cpp',
    'Pipeline.cpp',
    'Result.cpp',
    'Tools.cpp',
    'VideoFrame.cpp',
  ],
  include_directories: [ runtime_include, common_include ],
  dependencies: opk_deps,
  link_with: [ common ],
  cpp_args: ['-DFMT_HEADER_ONLY'],
  gnu_symbol_visibility: 'default',
  name_prefix: '',
  install: release_package,
  install_dir: get_option('libdir') / 'opk',
  install_rpath: '$ORIGIN',
)
