stdops = shared_module(
  'pek-std-ops',                   
  sources: [
    'Plugin.cpp',
    'GenericPostprocessOp.cpp',
    'GenericImagePreprocessOp.cpp',
    'InferenceControllerOp.cpp',

    'postproc/RvmParser.cpp',
    'postproc/DummyParser.cpp',
    'postproc/YoloParser.cpp',
    'postproc/YoloXParser.cpp',
    'postproc/UltrafaceParser.cpp',
    'postproc/PaddleocrParser.cpp',
    'postproc/GazeDetectionParser.cpp',
    'postproc/CameraContactParser.cpp',
    'postproc/PersonClassificationParser.cpp',
    'postproc/ImageNetClassificationParser.cpp',
    'postproc/ObjectEmbeddingParser.cpp',
    'postproc/ModNetSegmentationParser.cpp',

    'preproc/GenericImageTensorBuilder.cpp',
    'preproc/CpuImageKernels.cpp'

  ],
  include_directories: common_include,
  dependencies: pek_deps ,
  cpp_args: ['-DFMT_HEADER_ONLY'],
  link_with: [ common ],
  name_prefix: '',
  gnu_symbol_visibility: 'default',
  install: false
)
