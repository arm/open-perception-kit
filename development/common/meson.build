
common = shared_library(
  'common',
  sources: [

    'gst/Tools.h',
    'gst/Tools.cpp',
    'gst/TensorMeta.h',
    'gst/TensorMeta.cpp',
    'gst/GstMetaWrapper.h',
    'gst/GstMetaWrapper.cpp',
    'gst/PerceptionContextMeta.h',
    'gst/PerceptionContextMeta.cpp',
    
    'amp/File.h',
    'amp/String.h',
    'amp/Result.h',
    'amp/Result.cpp',
    'amp/Tools.h',
    'amp/Tools.cpp',
    'amp/ImageNetLabels.cpp',
    'amp/CocoLabels.cpp',
    'amp/ModelDescriptor.h',
    'amp/ModelDescriptor.cpp',

    'amp/AudioBuffer.cpp',

    'op/Op.cpp',
    'op/OpRef.cpp',
    'op/OpChain.cpp',
    'op/OpChainContext.cpp',
    'op/OpChainDescriptor.cpp',

    'PerformanceTracer.h',
    'PerformanceTracer.cpp',
  ],
  include_directories: [ common_include ],  
  cpp_args: ['-DFMT_HEADER_ONLY'],
  dependencies: [ amp_deps ],
  gnu_symbol_visibility: 'default',
  install: false
)
