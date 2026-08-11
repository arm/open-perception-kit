
common = shared_library(
  'pek-common',
  sources: [

    'gst/Tools.cpp',
    'gst/TensorMeta.cpp',

    'pek/Result.cpp',
    'pek/Tools.cpp',
    'pek/Labels.cpp',
    'pek/Model.cpp',
    'pek/ModelDescriptor.cpp',

    'pek/AudioBuffer.cpp',

    'pek/Base64.cpp',

    'pek/FrameResults.cpp',

    'op/Op.cpp',
    'op/OpRef.cpp',
    'op/OpChain.cpp',
    'op/OpChainContext.cpp',
    'op/OpChainDescriptor.cpp',

    'perf/PerformanceTracer.cpp',
    'perf/PerformanceMetrics.cpp',

    'mediaio/Common.cpp',
    'mediaio/VideoFrame.cpp',
    'mediaio/GstVideoFrame.cpp',
    'mediaio/PixelBufferVideoFrame.cpp',
  ],
  include_directories: [ common_include ],
  dependencies: [ pek_deps, config_validator_dep ],
  link_whole: [logging],
  gnu_symbol_visibility: 'default',
  install: release_package,
  install_dir: 'lib/pek',
  install_rpath: '$ORIGIN',
)
