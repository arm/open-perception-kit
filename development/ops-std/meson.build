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
    'postproc/ScrfdParser.cpp',

    'preproc/GenericImageTensorBuilder.cpp',
    'preproc/CpuImageKernels.cpp',
    'preproc/CpuImageKernelsI420.cpp',
    'preproc/CpuImageKernelsNv12.cpp',
    'preproc/CpuImageKernelsYuy2.cpp'

  ],
  include_directories: common_include,
  dependencies: pek_deps ,
  link_with: [ common ],
  name_prefix: '',
  gnu_symbol_visibility: 'default',
  install: release_package,
  install_dir: 'lib/pek',
  install_rpath: '$ORIGIN',
)
