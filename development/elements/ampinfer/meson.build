
gstampinfer = shared_module(
  'ampinfer',                   
  sources: [
    'ampinfer.cpp'
  ],
  include_directories: common_include,
  dependencies: amp_deps,
  cpp_args: ['-DFMT_HEADER_ONLY'],
  link_with: [ common ],
  gnu_symbol_visibility: 'hidden',
  install: false
)
