# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates

common = shared_library(
  'opk-common',
  sources: [

    'gst/Tools.cpp',
    'gst/TensorMeta.cpp',

    'opk/Result.cpp',
    'opk/Tools.cpp',
    'opk/Labels.cpp',
    'opk/Model.cpp',
    'opk/ModelDescriptor.cpp',
    'opk/PipelinePreset.cpp',

    'opk/AudioBuffer.cpp',

    'opk/Base64.cpp',

    'opk/FrameResults.cpp',

    'op/Op.cpp',
    'op/OpRef.cpp',
    'op/OpChain.cpp',
    'op/OpChainContext.cpp',
    'op/OpChainDescriptor.cpp',

    'perf/PerformanceMetrics.cpp',

    'raster/BitmapFont.cpp',
    'raster/SegmentationMask.cpp',
    'raster/SurfacePainter.cpp',

    'mediaio/Common.cpp',
    'mediaio/VideoFrame.cpp',
    'mediaio/GstVideoFrame.cpp',
    'mediaio/PixelBufferVideoFrame.cpp',
  ],
  include_directories: [ common_include ],
  dependencies: [ opk_deps, config_validator_dep ],
  link_whole: [logging],
  gnu_symbol_visibility: 'default',
  install: release_package,
  install_dir: 'lib/opk',
  install_rpath: '$ORIGIN',
)
