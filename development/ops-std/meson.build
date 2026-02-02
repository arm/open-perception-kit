
stdops = shared_module(
  'amp-std-ops',                   
  sources: [
    'Interface.cpp',
    'Postprocess.cpp',
    'ImagePreprocessOp.cpp'
  ],
  include_directories: common_include,
  dependencies: amp_deps,
  cpp_args: ['-DFMT_HEADER_ONLY'],
  link_with: [ common ],
  name_prefix: '',
  gnu_symbol_visibility: 'default',
  install: false
)
