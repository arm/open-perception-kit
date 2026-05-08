
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
  install: false,
  dependencies: [nlohmann_dep, pek_menu_parser_dep]
)
