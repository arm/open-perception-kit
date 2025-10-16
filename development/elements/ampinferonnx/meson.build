
ampinferonnx = shared_module(
  'ampinferonnx',                   
  sources: [
    'ampinferonnx.cpp'
  ],
  include_directories: [ common_include, uniflow_include ],
  dependencies: [amp_deps ], 
  link_with: [ common ],
  gnu_symbol_visibility: 'hidden', 
  install: false
)
