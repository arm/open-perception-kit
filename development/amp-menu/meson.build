
amp_menu_parser_inc = include_directories('.')
amp_menu_parser_lib = static_library(
  'amp_menu_parser',
  sources: ['parsar.cpp'],
  include_directories: [amp_menu_parser_inc],
  install: false,
)

amp_menu_parser_dep = declare_dependency(
  link_with: amp_menu_parser_lib,
  include_directories: [amp_menu_parser_inc],
)

amp_menu = executable(
  'amp-menu',
  sources: ['amp-menu.cpp'],
  install: false,
  dependencies: [nlohmann_dep, amp_menu_parser_dep]
)
