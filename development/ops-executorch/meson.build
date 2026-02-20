executorch_opt = get_option('executorch')

if executorch_opt.disabled()
  subdir_done()
endif

executorch_root = meson.project_source_root() / '../deps/executorch'
libtorch_root   = meson.project_source_root() / '../deps/libtorch'
executorch_libdir = executorch_root / 'lib'

executorch_dep = declare_dependency(
  include_directories: [
    include_directories('../deps/executorch/include'),
    include_directories('../deps/libtorch/include'),
  ],
  link_args: [
      '-L' + executorch_libdir,
      '-lextension_module',
      '-lextension_tensor',
      '-lextension_flat_tensor',
      '-lextension_data_loader',
      '-lextension_named_data_map',
      '-lextension_threadpool',
      '-lexecutorch',
      '-lexecutorch_core',
      '-lpthreadpool',
      '-lcpuinfo',
      '-lpthread',
      '-ldl',
      '-lm',
      '-Wl,--no-undefined',
  ]
)

# These are the usual “registration / kernels” libs that often get dropped unless whole-archived
et_force_archives = [
  executorch_libdir / 'libportable_ops_lib.a',
  executorch_libdir / 'libportable_kernels.a',
  executorch_libdir / 'libkleidiai.a',
  executorch_libdir / 'libXNNPACK.a',
  executorch_libdir / 'libxnnpack_backend.a',
  executorch_libdir / 'libxnnpack-microkernels-prod.a',
#  executorch_libdir / 'libkernels_util_all_deps.a',
]

executorchops = shared_module(
  'amp-executorch-ops',
  sources: [
    'Inference.cpp',
    'Interface.cpp',
    'InferenceOp.cpp',
  ],
  include_directories: common_include,
  dependencies: amp_deps + [executorch_dep],
  cpp_args: ['-DFMT_HEADER_ONLY'],
  link_with: [common],

  link_args: [
    '-Wl,--whole-archive',
  ] + et_force_archives + [
    '-Wl,--no-whole-archive',
    '-Wl,--no-undefined',
  ],

  name_prefix: '',
  gnu_symbol_visibility: 'default',
  install: false,
)
