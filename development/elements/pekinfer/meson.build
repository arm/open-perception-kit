
gstpekinfer = shared_module(
  'pekinfer',                   
  sources: [
    'pekinfer.cpp'
  ],
  include_directories: common_include,
  dependencies: pek_deps,
  cpp_args: ['-DFMT_HEADER_ONLY'],
  link_with: [ common ],
  gnu_symbol_visibility: 'hidden',
  install: false
)
