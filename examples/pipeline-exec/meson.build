project(
  'pipeline-exec-example',
  'cpp',
  version: '0.1.0',
  default_options: [
    'cpp_std=c++20',
    'warning_level=2',
  ]
)

fs = import('fs')
fmt_dep = dependency(
  'fmt',
  version: ['>=12.0.0', '<13.0.0'],
  fallback: ['fmt', 'fmt_dep'],
  required: true,
)
flatbuffers_dep = dependency('flatbuffers', version: '==25.9.23', required: false)
if not flatbuffers_dep.found()
  flatbuffers_dep = declare_dependency(
    include_directories: include_directories(
      '../../development/subprojects/flatbuffers-25.9.23/include',
    ),
    version: '25.9.23',
  )
endif

repo_root = meson.project_source_root() / '../..'
development_include = include_directories(
  '../common',
  '../../development',
  '../../generated/perception/cpp',
  '../../development/subprojects/expected-1.3.1/include',
  '../../development/subprojects/magic_enum-0.9.7/include',
  '../../development/subprojects/nlohmann_json-3.12.0/include',
)
runtime_build_dir = repo_root / 'development/build-active/meson-out'
runtime_library = runtime_build_dir / 'pek-runtime.so'
common_library = runtime_build_dir / 'libpek-common.so'

if not fs.exists(runtime_library) or not fs.exists(common_library)
  warning('pek-runtime.so or libpek-common.so was not found. Build the main PEK tree first, e.g. ./scripts/build.sh debug true')
endif

runtime_dep = declare_dependency(
  include_directories: development_include,
  link_args: [
    '-L' + runtime_build_dir,
    '-l:pek-runtime.so',
    '-l:libpek-common.so',
    '-Wl,-rpath,' + runtime_build_dir,
  ],
)

executable(
  'pipeline-exec',
  'main.cpp',
  '../common/PerceptionPacket.cpp',
  '../common/TextDisplay.cpp',
  dependencies: [ runtime_dep, fmt_dep, flatbuffers_dep ],
  install: false,
)
