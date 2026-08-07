
onnxops = shared_module(
  'pek-onnx-ops',                   
  sources: [
    'Inference.cpp',
    'InferenceOp.cpp',
    'Plugin.cpp'
  ],
  include_directories: common_include,
  dependencies: [pek_deps, onnx_dep],
  link_with: [ common ],
  name_prefix: '',
  gnu_symbol_visibility: 'default',
  install: release_package,
  install_dir: 'lib/pek',
  install_rpath: '$ORIGIN',
)
