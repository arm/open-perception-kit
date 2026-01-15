
common = shared_library(
  'common',
  sources: [
    'ModelDescriptor.h',
    'ModelDescriptor.cpp',
    'PerformanceMetrics.h',
    'PerformanceMetrics.cpp',

    'gst/Tools.h',
    'gst/Tools.cpp',
    'gst/TensorMeta.h',
    'gst/TensorMeta.cpp',

    'amp/File.h',
    'amp/Painter.h',
    'amp/String.h',
    'amp/Result.h',
    'amp/Result.cpp',
    'amp/Tools.h',
    'amp/Tools.cpp',
    'amp/PerceptionContext.h',
    'amp/PerceptionContext.cpp',
    'amp/ImageNetLabels.cpp',
    'amp/CocoLabels.cpp',

    'onnx/Inference.h',
    'onnx/Inference.cpp',
    'onnx/Tools.h',
    'onnx/Tools.cpp',

    'postproc/YoloParser.cpp',
    'postproc/UltraFaceParser.cpp',
    'postproc/PaddleOcrParser.cpp',

    'preproc/ImageTensorBuilder.cpp',
    'preproc/CpuImageKernels.cpp',

    'PerformanceTracer.h',
    'PerformanceTracer.cpp',
  ],
  include_directories: [ common_include ],  
  cpp_args: ['-DFMT_HEADER_ONLY'],
  dependencies: [ amp_deps ],
  gnu_symbol_visibility: 'default',
  install: false
)
