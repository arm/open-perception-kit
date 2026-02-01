
stdops = shared_module(
  'amp-std-ops',                   
  sources: [
    'Plugin.cpp',
    'GenericPostprocessOp.cpp',
    'GenericImagePreprocessOp.cpp',

    'postproc/YoloParser.cpp',
    'postproc/UltrafaceParser.cpp',
    'postproc/PaddleocrParser.cpp',
    'postproc/GazeDetectionParser.cpp',

    'preproc/GenericImageTensorBuilder.cpp',
    'preproc/CpuImageKernels.cpp'

  ],
  include_directories: common_include,
  dependencies: amp_deps,
  cpp_args: ['-DFMT_HEADER_ONLY'],
  link_with: [ common ],
  name_prefix: '',
  gnu_symbol_visibility: 'default',
  install: false
)
