
common = shared_library(
  'common',
  sources: [

    'gst/Tools.cpp',
    'gst/TensorMeta.cpp',
    
    'pek/Result.cpp',
    'pek/Tools.cpp',
    'pek/Labels.cpp',
    'pek/Model.cpp',
    'pek/ModelDescriptor.cpp',

    'pek/AudioBuffer.cpp',

    'pek/PerceptionSerializer.cpp',

    'op/Op.cpp',
    'op/OpRef.cpp',
    'op/OpChain.cpp',
    'op/OpChainContext.cpp',
    'op/OpChainDescriptor.cpp',

    'PerformanceTracer.cpp',
  ],
  include_directories: [ common_include ],  
  cpp_args: ['-DFMT_HEADER_ONLY'],
  dependencies: [ pek_deps ],
  gnu_symbol_visibility: 'default',
  install: false
)
