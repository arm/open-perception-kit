pipeline_exec = executable(
  'pipeline-exec',
  'main.cpp',
  example_common_sources,
  build_rpath: '$ORIGIN/',
  include_directories: [example_common_include, common_include, runtime_include],
  dependencies: [fmt_dep, magic_enum_dep, nlohmann_dep, tl_expected_dep, perception_dep],
  link_with: [opk_runtime, common],
  install: false,
)
