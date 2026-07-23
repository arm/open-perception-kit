project(
  'yolo-benchmark-example',
  'cpp',
  version: '0.1.0',
  default_options: [
    'cpp_std=c++20',
    'warning_level=2',
  ]
)

fs = import('fs')
fmt_dep = dependency('fmt')

repo_root = meson.project_source_root() / '../../..'
development_include = include_directories(
  '../../../development',
  '../../../development/common',
  '../../../development/subprojects/expected-1.3.1/include',
  '../../../development/subprojects/magic_enum-0.9.7/include',
  '../../../development/subprojects/nlohmann_json-3.12.0/include',
)
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
  ],
)

executable(
  'yolo-benchmark',
  'benchmark.cpp',
  dependencies: [ runtime_dep, fmt_dep ],
  install: false,
)

executable(
  'yolo-video-benchmark',
  'video_benchmark.cpp',
  dependencies: [ runtime_dep, fmt_dep ],
  install: false,
)
