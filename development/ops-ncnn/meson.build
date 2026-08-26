ncnn_opt = get_option('ncnn')

if ncnn_opt.disabled()
  subdir_done()
endif

ncnn_candidates = []
ncnn_root_env = run_command('sh', '-c', 'printf %s "$PEK_NCNN_ROOT"', check: false).stdout().strip()
if ncnn_root_env != ''
  ncnn_candidates += [ncnn_root_env]
endif

ncnn_candidates += [
  pek_repository_root / 'deps/ncnn',
]

ncnn_root = ''

foreach candidate : ncnn_candidates
  candidate_libdir = candidate / 'lib'
  candidate_has_lib = fs.is_file(candidate_libdir / 'libncnn.a') or fs.is_file(candidate_libdir / 'libncnn.so')
  if fs.is_file(candidate / 'include/ncnn/net.h') and fs.is_dir(candidate_libdir) and candidate_has_lib
    ncnn_root = candidate
    break
  endif
endforeach

if ncnn_root == ''
  if ncnn_opt.enabled()
    error('NCNN requested (ncnn=enabled) but not found. Stage an NCNN SDK with include/ and lib/, or set PEK_NCNN_ROOT.')
  endif

  warning('NCNN not found - skipping ops-ncnn (configure with -Dncnn=enabled to require it).')
  subdir_done()
endif

message('Using NCNN from: ' + ncnn_root)

ncnn_libdir = ncnn_root / 'lib'
ncnn_include = ncnn_root / 'include'
openmp_dep = dependency('openmp', language: 'cpp', required: ncnn_opt.enabled())

if not openmp_dep.found()
  if ncnn_opt.enabled()
    error('NCNN requested (ncnn=enabled) but OpenMP was not found. The staged NCNN library requires OpenMP.')
  endif

  warning('OpenMP not found - skipping ops-ncnn because the staged NCNN library requires it.')
  subdir_done()
endif

ncnn_link_args = [
  '-L' + ncnn_libdir,
  '-lncnn',
  '-lpthread',
  '-ldl',
  '-lm',
  '-Wl,--no-undefined',
]

ncnn_static_vulkan_link_args = []
foreach lib : [
  'glslang',
  'SPIRV',
  'OSDependent',
  'MachineIndependent',
  'GenericCodeGen',
  'glslang-default-resource-limits',
]
  if fs.is_file(ncnn_libdir / ('lib' + lib + '.a'))
    ncnn_static_vulkan_link_args += ['-l' + lib]
  endif
endforeach

if ncnn_static_vulkan_link_args.length() > 0
  message('Using NCNN Vulkan static link dependencies: ' + ' '.join(ncnn_static_vulkan_link_args))
  ncnn_link_args = [
    '-L' + ncnn_libdir,
    '-Wl,--start-group',
    '-lncnn',
  ] + ncnn_static_vulkan_link_args + [
    '-Wl,--end-group',
    '-lpthread',
    '-ldl',
    '-lm',
    '-Wl,--no-undefined',
  ]
endif

ncnn_dep = declare_dependency(
  compile_args: [
    '-isystem',
    ncnn_include,
  ],
  link_args: ncnn_link_args
)

ncnnops = shared_module(
  'pek-ncnn-ops',
  sources: [
    'Inference.cpp',
    'Plugin.cpp',
    'InferenceOp.cpp',
  ],
  include_directories: common_include,
  dependencies: pek_deps + [ncnn_dep, openmp_dep],
  cpp_args: ['-DFMT_HEADER_ONLY'],
  link_with: [common],
  name_prefix: '',
  gnu_symbol_visibility: 'default',
  install: false,
)
