
opk_menu = executable(
  'opk-menu',
  sources: ['opk-menu.cpp', 'menu.cpp', 'discover.cpp'],
  # build.sh stages this executable beside its Runtime/common libraries in the
  # repository's tools directory.
  build_rpath: '$ORIGIN/',
  include_directories: [common_include, runtime_include],
  install: false,
  dependencies: [fmt_dep, magic_enum_dep, nlohmann_dep, tl_expected_dep, perception_dep],
  link_with: [opk_runtime, common],
)
