
gstopkinfer = shared_module(
  'opkinfer',                   
  sources: [
    'opkinfer.cpp'
  ],
  include_directories: common_include,
  dependencies: opk_deps,
  link_with: [ common ],
  gnu_symbol_visibility: 'hidden',
  install: release_package,
  install_dir: 'lib/gstreamer-1.0',
  install_rpath: '$ORIGIN/../opk',
)

executable(
  'opkinfer-retry-valgrind-test',
  [
    'opkinfer_retry_valgrind_test.cpp',
    '../../ops-onnx/Inference.cpp',
  ],
  include_directories: [common_include, include_directories('../../ops-onnx')],
  dependencies: [opk_deps, onnx_dep],
  link_with: [common],
  build_by_default: false,
  install: false,
)
