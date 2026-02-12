
t = executable(
  'unit_tests',
  ['img_transforms.cc'],
  # IMPORTANT: keep order: gtest_main first, then gtest (static link order)
  dependencies : [gtest_main_dep, gtest_dep, threads_dep, amp_deps],
  # include_directories : [common_include],
)


test('unit', t)


