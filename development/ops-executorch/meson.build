executorch_opt = get_option('executorch')

if executorch_opt.disabled()
  subdir_done()
endif

executorch_candidates = []
executorch_root_env = run_command('sh', '-c', 'printf %s "$PEK_EXECUTORCH_ROOT"', check: false).stdout().strip()
if executorch_root_env != ''
  executorch_candidates += [executorch_root_env]
else
  executorch_candidates += ['/opt/pek-deps/executorch']
endif

executorch_root = ''

foreach candidate : executorch_candidates
  candidate_libdir = candidate / 'lib'
  if (
    fs.is_file(candidate / 'include/executorch/extension/module/module.h') and
    fs.is_file(candidate / 'include/executorch/extension/tensor/tensor_ptr.h') and
    fs.is_file(candidate / 'include/executorch/extension/tensor/tensor_ptr_maker.h') and
    fs.is_file(candidate / 'include/executorch/runtime/core/error.h') and
    fs.is_file(candidate / 'include/executorch/runtime/core/evalue.h') and
    fs.is_file(candidate / 'include/executorch/runtime/core/portable_type/c10/c10/util/irange.h') and
    fs.is_dir(candidate_libdir)
  )
    executorch_root = candidate
    break
  endif
endforeach

if executorch_root == ''
  if executorch_opt.enabled()
    error('ExecuTorch requested (executorch=enabled) but not found. Install libexecutorch-dev under /opt/pek-deps, or set PEK_EXECUTORCH_ROOT to a staged SDK.')
  endif

  warning('Installed ExecuTorch SDK not found - skipping ops-executorch. Install libexecutorch-dev, or set PEK_EXECUTORCH_ROOT to use a staged SDK.')
  subdir_done()
endif

message('Using ExecuTorch from: ' + executorch_root)

executorch_libdir = executorch_root / 'lib'
executorch_include = executorch_root / 'include'
executorch_portable_type_include = executorch_include / 'executorch/runtime/core/portable_type'
executorch_c10_include = executorch_include / 'executorch/runtime/core/portable_type/c10'
executorch_uses_kleidiai = host_machine.cpu_family() == 'aarch64'

executorch_required_libs = [
  'libextension_module.a',
  'libextension_tensor.a',
  'libextension_flat_tensor.a',
  'libextension_data_loader.a',
  'libextension_named_data_map.a',
  'libextension_threadpool.a',
  'libexecutorch.a',
  'libexecutorch_core.a',
  'libpthreadpool.a',
  'libcpuinfo.a',
  'libportable_ops_lib.a',
  'libportable_kernels.a',
]

if executorch_uses_kleidiai
  executorch_required_libs += ['libkleidiai.a']
endif

executorch_required_libs += [
  'libXNNPACK.a',
  'libxnnpack_backend.a',
  'libxnnpack-microkernels-prod.a',
]

executorch_missing_libs = []
foreach lib : executorch_required_libs
  if not fs.is_file(executorch_libdir / lib)
    executorch_missing_libs += [lib]
  endif
endforeach

if executorch_missing_libs.length() > 0
  if executorch_opt.enabled()
    error('ExecuTorch SDK is incomplete under ' + executorch_root + '. Missing: ' + ', '.join(executorch_missing_libs))
  endif

  warning('ExecuTorch SDK is incomplete - skipping ops-executorch. Missing: ' + ', '.join(executorch_missing_libs))
  subdir_done()
endif

executorch_dep = declare_dependency(
  compile_args: [
    '-isystem',
    executorch_include,
    '-isystem',
    executorch_portable_type_include,
    '-isystem',
    executorch_c10_include,
    '-DC10_USING_CUSTOM_GENERATED_MACROS',
  ],
  link_args: [
      '-L' + executorch_libdir,
      '-lextension_module',
      '-lextension_tensor',
      '-lextension_flat_tensor',
      '-lextension_data_loader',
      '-lextension_named_data_map',
      '-lextension_threadpool',
      '-lexecutorch_core',
      '-lpthreadpool',
      '-lcpuinfo',
      '-lpthread',
      '-ldl',
      '-lm',
      '-Wl,--no-undefined',
  ]
)

# ExecuTorch registers primitive ops, backends, and kernels from static
# initializers, so these archives must not be dead-stripped.
et_force_archives = [
  executorch_libdir / 'libexecutorch.a',
  executorch_libdir / 'libportable_ops_lib.a',
  executorch_libdir / 'libportable_kernels.a',
]

if executorch_uses_kleidiai
  et_force_archives += [executorch_libdir / 'libkleidiai.a']
endif

et_force_archives += [
  executorch_libdir / 'libXNNPACK.a',
  executorch_libdir / 'libxnnpack_backend.a',
  executorch_libdir / 'libxnnpack-microkernels-prod.a',
]

executorch_runtime_dep = declare_dependency(
  dependencies: [executorch_dep],
  link_args: [
    '-Wl,--whole-archive',
  ] + et_force_archives + [
    '-Wl,--no-whole-archive',
    '-Wl,--no-undefined',
  ],
)

executorchops = shared_module(
  'pek-executorch-ops',
  sources: [
    'Inference.cpp',
    'Plugin.cpp',
    'InferenceOp.cpp',
  ],

  include_directories: common_include,

  dependencies: pek_deps + [executorch_runtime_dep],
  cpp_args: ['-DFMT_HEADER_ONLY'],

  link_args: ['-Wl,--no-undefined'],

  name_prefix: '',
  gnu_symbol_visibility: 'default',
  link_with: [common, model_loading],

  install: false,
)
