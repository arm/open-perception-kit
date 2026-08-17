
pek_menu_parser_inc = include_directories('.')
pek_menu_parser_lib = static_library(
  'pek_menu_parser',
  sources: ['parser.cpp'],
  include_directories: [pek_menu_parser_inc],
  install: false,
)

pek_menu_parser_dep = declare_dependency(
  link_with: pek_menu_parser_lib,
  include_directories: [pek_menu_parser_inc],
)

pek_menu = executable(
  'pek-menu',
  sources: ['pek-menu.cpp'],
  # build-elements.sh stages this executable beside libpek-common.so in the
  # repository's tools directory.
  build_rpath: '$ORIGIN/',
  include_directories: [common_include],
  install: false,
  dependencies: [fmt_dep, nlohmann_dep, pek_menu_parser_dep],
  link_with: [common],
)
