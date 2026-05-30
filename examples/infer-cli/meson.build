project(
  'infer-cli-example',
  'cpp',
  version: '0.1.0',
  default_options: [
    'cpp_std=c++20',
    'warning_level=2',
  ]
)

fs = import('fs')

repo_root = meson.project_source_root() / '../..'
common_include = include_directories(
  '../../development/common',
  '../../development/subprojects/expected-1.3.1/include',
  '../../development/subprojects/magic_enum-0.9.7/include',
  '../../development/subprojects/nlohmann_json-3.12.0/include',
)
common_build_dir = repo_root / 'development/build/meson-out'
common_library = common_build_dir / 'libcommon.so'

if not fs.exists(common_library)
  warning('libcommon.so was not found. Build the main PEK tree first, e.g. ./scripts/build-elements.sh debug true')
endif

common_dep = declare_dependency(
  include_directories: common_include,
  link_args: [
    '-L' + common_build_dir,
    '-lcommon',
    '-Wl,-rpath,' + common_build_dir,
  ],
)

executable(
  'infer-cli',
  'main.cpp',
  dependencies: common_dep,
  install: false,
)
