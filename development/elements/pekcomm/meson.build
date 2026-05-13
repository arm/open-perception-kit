
pekcomm = shared_module(
  'pekcomm',                   
  sources: [
    'pekcomm.cpp',
    'writer.cpp',
    'file_writer.cpp',
  ],
  include_directories: [common_include],
  dependencies: [pek_deps, dependency('zlib')], 
  gnu_symbol_visibility: 'hidden', 
  link_with: [ common ],
  install: false
)

