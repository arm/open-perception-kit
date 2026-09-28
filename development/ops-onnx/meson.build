# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates


onnxops = shared_module(
  'opk-onnx-ops',
  sources: [
    'Inference.cpp',
    'InferenceOp.cpp',
    'Plugin.cpp'
  ],
  include_directories: common_include,
  dependencies: [opk_deps, onnx_dep],
  link_with: [ common ],
  name_prefix: '',
  gnu_symbol_visibility: 'default',
  install: release_package,
  install_dir: 'lib/opk',
  install_rpath: '$ORIGIN',
)
