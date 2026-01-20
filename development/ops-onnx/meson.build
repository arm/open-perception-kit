
onnxops = shared_module(
  'amp-onnx-ops',                   
  sources: [
    'Inference.cpp',
    'Interface.cpp',
    'PreprocessInference.cpp'    
  ],
  include_directories: common_include,
  dependencies: amp_deps,
  cpp_args: ['-DFMT_HEADER_ONLY'],
  link_with: [ common ],
  name_prefix: '',
  gnu_symbol_visibility: 'default',
  install: false
)
