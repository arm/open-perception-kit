api_include = include_directories('..')

pek_api = shared_library(
  'pekapi',
  sources: [
    'Pipeline.cpp',
    'Result.cpp',
  ],
  include_directories: [ api_include, common_include ],
  dependencies: pek_deps,
  link_with: [ common ],
  cpp_args: ['-DFMT_HEADER_ONLY'],
  gnu_symbol_visibility: 'default',
  install: false,
)
