
common = static_library(
  'common',                   
  sources: [
    'GstTools.h',
    'GstTools.cpp',
    'ModelDescriptor.h',
    'ModelDescriptor.cpp',
    'AmpTools.h',
    'AmpTools.cpp',
    'PerformanceMetrics.h',
    'PerformanceMetrics.cpp',
    'OnnxTools.h',
    'OnnxTools.cpp',
    'TensorMeta.h',
    'TensorMeta.cpp',
    'InferResultMeta.h',
    'InferResultMeta.cpp',

    'amp/Result.h',
    'amp/Result.cpp',

    'onnx/Inference.h',
    'onnx/Inference.cpp',
    'onnx/Tools.h',
    'onnx/Tools.cpp'
  ],
  include_directories: [ common_include ],
  cpp_args: ['-DFMT_HEADER_ONLY'],
  dependencies: [ amp_deps ],
  gnu_symbol_visibility: 'hidden', 
  install: false
)
