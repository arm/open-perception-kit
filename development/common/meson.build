
subdir('logging')

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

    'perf/PerformanceTracer.cpp',

    'mediaio/Common.cpp',
    'mediaio/VideoFrame.cpp',
    'mediaio/GstVideoFrame.cpp',
    'mediaio/PixelBufferVideoFrame.cpp',
  ],
  include_directories: [ common_include ],
  cpp_args: ['-DFMT_HEADER_ONLY'],
  dependencies: [ pek_deps ],
  link_whole: [logging],
  gnu_symbol_visibility: 'default',
  install: false
)

model_loading = shared_library(
  'pek-model-loading',
  sources: [
    'pek/ModelDescriptorLoader.cpp',
  ],
  include_directories: [ common_include ],
  cpp_args: ['-DFMT_HEADER_ONLY'],
  dependencies: [ pek_deps, modelfetch_dep ],
  link_with: [ common ],
  gnu_symbol_visibility: 'default',
  install: false
)
