
common = static_library(
  'common',                   
  sources: [
    'GstTools.h',
    'GstTools.cpp',
    'AmpTools.h',
    'AmpTools.cpp',
    'PerformanceMetrics.h',
    'PerformanceMetrics.cpp',
    'OnnxTools.h',
    'OnnxTools.cpp',
    'OnnxInference.h',
    'OnnxInference.cpp',
    'TensorMeta.h',
    'TensorMeta.cpp',
    'InferResultMeta.h',
    'InferResultMeta.cpp'
  ],
#  include_directories: [ common_include, uniflow_include],
  include_directories: [ common_include ],
  dependencies: [ amp_deps ],
  gnu_symbol_visibility: 'hidden', 
  install: false
)
