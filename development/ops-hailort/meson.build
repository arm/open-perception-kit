hailort_opt = get_option('hailort')

if hailort_opt.disabled()
  subdir_done()
endif

hailort_dep = dependency('hailort', required: hailort_opt.enabled())

if not hailort_dep.found()
  cc = meson.get_compiler('cpp')
  hailort_lib = cc.find_library('hailort', required: hailort_opt.enabled())
  hailort_header = cc.has_header('hailo/hailort.hpp', required: hailort_opt.enabled())

  if hailort_lib.found() and hailort_header
    hailort_dep = declare_dependency(
      include_directories: include_directories('/usr/include'),
      dependencies: [hailort_lib],
    )
  endif
endif

if not hailort_dep.found()
  if hailort_opt.enabled()
    error('HailoRT requested (hailort=enabled) but not found. Install libhailort + headers (hailo/hailort.hpp), or configure with -Dhailort=disabled.')
  endif

  warning('HailoRT not found — skipping ops-hailort (configure with -Dhailort=enabled to require it).')
  subdir_done()
endif

hailortops = shared_module(
  'pek-hailort-ops',
  sources: [
    'Inference.cpp',
    'Plugin.cpp',
    'InferenceOp.cpp'
  ],
  include_directories: common_include,
  dependencies: pek_deps + [hailort_dep],
  link_with: [ common ],
  name_prefix: '',
  gnu_symbol_visibility: 'default',
  install: false
)
