
ampcomm = shared_module(
  'ampcomm',                   
  sources: [
    'ampcomm.cpp',
    'writer.cpp',
    'file_writer.cpp',
  ],
  include_directories: [common_include],
  dependencies: [amp_deps, dependency('zlib')], 
  gnu_symbol_visibility: 'hidden', 
  link_with: [ common ],
  install: false
)

