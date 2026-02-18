
common = shared_library(
  'common',
  sources: [

    'gst/Tools.cpp',
    'gst/TensorMeta.cpp',
    'gst/PerceptionContextMeta.cpp',
    
    'amp/Result.cpp',
    'amp/Tools.cpp',
    'amp/ImageNetLabels.cpp',
    'amp/CocoLabels.cpp',
    'amp/ModelDescriptor.cpp',

    'op/Op.cpp',
    'op/OpRef.cpp',
    'op/OpChain.cpp',
    'op/OpChainContext.cpp',
    'op/OpChainDescriptor.cpp',

    'PerformanceTracer.cpp',
  ],
  include_directories: [ common_include ],  
  cpp_args: ['-DFMT_HEADER_ONLY'],
  dependencies: [ amp_deps ],
  gnu_symbol_visibility: 'default',
  install: false
)
