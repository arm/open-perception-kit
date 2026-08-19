
gstpekinfer = shared_module(
  'pekinfer',                   
  sources: [
    'pekinfer.cpp'
  ],
  include_directories: common_include,
  dependencies: pek_deps,
  link_with: [ common ],
  gnu_symbol_visibility: 'hidden',
  install: release_package,
  install_dir: 'lib/gstreamer-1.0',
  install_rpath: '$ORIGIN/../pek',
)

executable(
  'pekinfer-retry-valgrind-test',
  [
    'pekinfer_retry_valgrind_test.cpp',
    '../../ops-onnx/Inference.cpp',
  ],
  include_directories: [common_include, include_directories('../../ops-onnx')],
  dependencies: [pek_deps, onnx_dep],
  link_with: [common],
  build_by_default: false,
  install: false,
)
