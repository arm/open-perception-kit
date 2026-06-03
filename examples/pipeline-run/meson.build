project(
  'pipeline-run-example',
  'cpp',
  version: '0.1.0',
  default_options: [
    'cpp_std=c++20',
    'warning_level=2',
  ]
)

fs = import('fs')
fmt_dep = dependency('fmt')

repo_root = meson.project_source_root() / '../..'
development_include = include_directories(
  '../../development',
  '../../development/subprojects/expected-1.3.1/include',
  '../../development/subprojects/nlohmann_json-3.12.0/include',
)
<<<<<<< HEAD:examples/pipeline-run/meson.build
common_build_dir = repo_root / 'development/build/meson-out'
api_library = common_build_dir / 'libpekapi.so'
if not fs.exists(api_library)
  warning('libpekapi.so was not found. Build the main PEK tree first, e.g. ./scripts/build-elements.sh debug true')
endif

api_dep = declare_dependency(
  include_directories: development_include,
  link_args: [
    '-L' + common_build_dir,
    '-lpekapi',
    '-Wl,-rpath,' + common_build_dir,
=======
runtime_build_dir = repo_root / 'development/build/meson-out'
runtime_library = runtime_build_dir / 'pek-runtime.so'
common_library = runtime_build_dir / 'libcommon.so'

if not fs.exists(runtime_library) or not fs.exists(common_library)
  warning('pek-runtime.so or libcommon.so was not found. Build the main PEK tree first, e.g. ./scripts/build-elements.sh debug true')
endif

runtime_dep = declare_dependency(
  include_directories: development_include,
  link_args: [
    '-L' + runtime_build_dir,
    '-l:pek-runtime.so',
    '-lcommon',
    '-Wl,-rpath,' + runtime_build_dir,
>>>>>>> abf198c (runtime api):examples/pipeline-exec/meson.build
  ],
)

executable(
  'pipeline-run',
  'main.cpp',
<<<<<<< HEAD:examples/pipeline-run/meson.build
  dependencies: [ api_dep, fmt_dep ],
=======
  dependencies: [ runtime_dep, fmt_dep ],
>>>>>>> abf198c (runtime api):examples/pipeline-exec/meson.build
  install: false,
)
