
common = static_library(
  'common',                   
  sources: [
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
