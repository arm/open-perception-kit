
opk_menu_cpp_args = []
if demo_deb_package
  opk_menu_cpp_args += [
    '-DOPK_DEFAULT_PROJECT_ROOT="/usr/share/opk"',
    '-DOPK_DEFAULT_PLUGIN_PATH="/usr/' + get_option('libdir') + '/gstreamer-1.0"',
    '-DOPK_DEFAULT_OPS_PATH="/usr/' + get_option('libdir') + '/opk"',
    '-DOPK_USE_XDG_STATE=1',
  ]
endif

opk_menu = executable(
  'opk-menu',
  sources: ['opk-menu.cpp', 'menu.cpp', 'discover.cpp'],
  # build.sh stages this executable beside its Runtime/common libraries in the
  # repository's tools directory.
  build_rpath: '$ORIGIN/',
  include_directories: [common_include, runtime_include],
  cpp_args: opk_menu_cpp_args,
  install: demo_deb_package,
  install_dir: get_option('bindir'),
  install_rpath: '$ORIGIN/../' + get_option('libdir') + '/opk',
  dependencies: [fmt_dep, magic_enum_dep, nlohmann_dep, tl_expected_dep, open_perception_kit_dep],
  link_with: [opk_runtime, common],
)
