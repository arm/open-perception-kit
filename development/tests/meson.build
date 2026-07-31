
pek_menu_tests = executable(
  'pek_menu_tests',
  ['pek_menu_parser_test.cpp'],
  dependencies : [gtest_main_dep, gtest_dep, pek_menu_parser_dep],
)

model_descriptor_tests = executable(
  'model_descriptor_tests',
  ['model_descriptor_test.cpp'],
  include_directories : [common_include],
  dependencies : [gtest_main_dep, gtest_dep, pek_deps],
  link_with : [common],
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

yolo_parser_tests = executable(
  'yolo_parser_tests',
  ['yolo_parser_test.cpp', '../ops-std/postproc/YoloParser.cpp'],
  include_directories : [common_include, include_directories('../ops-std')],
  cpp_args: ['-DFMT_HEADER_ONLY'],
  dependencies : [gtest_main_dep, gtest_dep, pek_deps],
  link_with : [common],
)

logging_tests = executable(
  'logging_tests',
  ['LoggingTest.cpp'],
  include_directories : [common_include],
  dependencies : [gtest_main_dep, gtest_dep, fmt_dep],
  link_with : [common],
)

logger_tests = executable(
  'logger_tests',
  ['LoggerTest.cpp'],
  include_directories : [common_include, logging_internal_include],
  dependencies : [gtest_main_dep, gtest_dep, fmt_dep, threads_dep],
  link_with : [logging],
)

logging_environment_probe = executable(
  'logging_environment_probe',
  ['LoggingEnvironmentProbe.cpp'],
  include_directories : [common_include],
  dependencies : [fmt_dep],
  link_with : [common],
)

pektracker_matching_tests = executable(
  'pektracker_matching_tests',
  ['pektracker_matching_test.cpp',
   '../elements/pektracker/Matching.cpp',
   '../elements/pektracker/TrackState.cpp',
   '../elements/pektracker/Utils.cpp'],
  include_directories : [common_include, include_directories('../elements/pektracker')],
  cpp_args: ['-DFMT_HEADER_ONLY'],
  dependencies : [gtest_main_dep, gtest_dep, pek_deps],
  link_with : [common],
)

webrtc_session_tests = executable(
  'webrtc_session_tests',
  ['webrtc_session_test.cpp',
   '../elements/peksink/webrtc_session.cpp',
   '../elements/peksink/utils.cpp'],
  include_directories : [common_include, include_directories('../elements/peksink')],
  dependencies : [gtest_main_dep, gtest_dep, pek_deps],
  link_with : [common],
)

runtime_pipeline_tests = executable(
  'runtime_pipeline_tests',
  ['runtime_pipeline_test.cpp'],
  include_directories : [common_include, include_directories('..')],
  dependencies : [gtest_main_dep, gtest_dep, pek_deps],
  link_with : [pek_runtime, common],
)

node = find_program('node', required : false)
python = import('python').find_installation()

test('pek_menu_tests', pek_menu_tests)
test(
  'pek_menu_cli_tests',
  python,
  args : [files('pek_menu_cli_test.py'), pek_menu.full_path()],
  depends : pek_menu,
)
test(
  'model_artifact_build_tests',
  python,
  args : [files('model_artifact_build_test.py')],
)
test('model_descriptor_tests', model_descriptor_tests)
test('matrix_operations_tests', matrix_operations_tests)
test('kalman_filter_tests', kalman_filter_tests)
test('hungarian_tests', hungarian_tests)
test('iou_tests', iou_tests)
test('yolo_parser_tests', yolo_parser_tests)
test('logging_tests', logging_tests)
test('logger_tests', logger_tests)
test(
  'logging_environment_tests',
  python,
  args : [files('logging_environment_test.py'), logging_environment_probe.full_path()],
  depends : logging_environment_probe,
)
test('pektracker_matching_tests', pektracker_matching_tests)
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
