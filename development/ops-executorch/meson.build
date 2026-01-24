
executorchops = shared_module(
  'amp-executorch-ops',                   
  sources: [
    'Inference.cpp',
    'Interface.cpp',
    'InferenceOp.cpp'    
  ],
  include_directories: common_include,
  dependencies: [amp_deps, executorch_dep],
  cpp_args: ['-DFMT_HEADER_ONLY'],
  link_with: [ common ],
  name_prefix: '',
  gnu_symbol_visibility: 'default',
  install: false
)
