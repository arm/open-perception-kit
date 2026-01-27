cmake = import('cmake')

opt = cmake.subproject_options()
opt.add_cmake_defines({
  'BUILD_GMOCK': false,
  'INSTALL_GTEST': false,
})

gtest_cmake = cmake.subproject('gtest', options : opt)

gtest_dep      = gtest_cmake.dependency('gtest')
gtest_main_dep = gtest_cmake.dependency('gtest_main')
threads_dep    = dependency('threads')

t = executable(
  'unit_tests',
  ['img_transforms.cc'],
  # IMPORTANT: keep order: gtest_main first, then gtest (static link order)
  dependencies : [gtest_main_dep, gtest_dep, threads_dep, amp_deps],
  # include_directories : [common_include],
)

test('unit', t)


