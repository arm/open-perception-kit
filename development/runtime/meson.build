runtime_include = include_directories('..')

pek_runtime = shared_library(
  'pek-runtime',
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
  dependencies: pek_deps,
  link_with: [ common ],
  cpp_args: ['-DFMT_HEADER_ONLY'],
  gnu_symbol_visibility: 'default',
  name_prefix: '',
  install: release_package,
  install_dir: 'lib/pek',
  install_rpath: '$ORIGIN',
)
