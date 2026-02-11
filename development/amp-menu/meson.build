
amp_menu = executable(
  'amp-menu',
  sources: ['amp-menu.cpp', 'parsar.cpp'],
  install: false,
  dependencies: [nlohmann_dep]
)

amp_menu_parsar_test = executable(
  'amp_menu_parsar_test',
  ['parsar.cpp', 'amp_menu_parsar_test.cpp'],
  # IMPORTANT: keep order: gtest_main first, then gtest (static link order)
  dependencies : [gtest_main_dep, gtest_dep],
  # include_directories : [common_include],
)

test('amp menu parsar test', amp_menu_parsar_test)
