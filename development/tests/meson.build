
pek_menu_tests = executable(
  'pek_menu_tests',
  ['pek_menu_parser_test.cpp'],
  dependencies : [gtest_main_dep, gtest_dep, pek_menu_parser_dep],
)

matrix_operations_tests = executable(
  'matrix_operations_tests',
  ['matrix_operations_test.cpp'],
  include_directories : [common_include],
  dependencies : [gtest_main_dep, gtest_dep],
)

kalman_filter_tests = executable(
  'kalman_filter_tests',
  ['kalman_filter_test.cpp'],
  include_directories : [common_include],
  dependencies : [gtest_main_dep, gtest_dep],
)

hungarian_tests = executable(
  'hungarian_tests',
  ['hungarian_test.cpp'],
  include_directories : [common_include],
  dependencies : [gtest_main_dep, gtest_dep],
)

iou_tests = executable(
  'iou_tests',
  ['iou_test.cpp'],
  include_directories : [common_include],
  dependencies : [gtest_main_dep, gtest_dep],
)

webrtc_session_tests = executable(
  'webrtc_session_tests',
  ['webrtc_session_test.cpp',
   '../elements/peksink/webrtc_session.cpp',
   '../elements/peksink/utils.cpp'],
  include_directories : [common_include, include_directories('../elements/peksink')],
  dependencies : [gtest_main_dep, gtest_dep, pek_deps],
)

runtime_pipeline_tests = executable(
  'runtime_pipeline_tests',
  ['runtime_pipeline_test.cpp'],
  include_directories : [common_include, include_directories('..')],
  dependencies : [gtest_main_dep, gtest_dep, pek_deps],
  link_with : [pek_runtime, common],
)

node = find_program('node', required : false)

test('pek_menu_tests', pek_menu_tests)
test('matrix_operations_tests', matrix_operations_tests)
test('kalman_filter_tests', kalman_filter_tests)
test('hungarian_tests', hungarian_tests)
test('iou_tests', iou_tests)
test('webrtc_session_tests', webrtc_session_tests)
test('runtime_pipeline_tests', runtime_pipeline_tests)

if node.found()
  test(
    'webrtc_client_tests',
    node,
    args : [
      '--test',
      meson.project_source_root() / 'web/content/webrtc_client.test.mjs',
      meson.project_source_root() / 'web/content/webrtc_config.test.mjs',
    ],
  )
endif
