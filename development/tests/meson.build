
jsoncons_dep = dependency(
  'jsoncons',
  required : true,
  fallback : ['jsoncons', 'jsoncons_dep'],
)

pek_menu_tests = executable(
  'pek_menu_tests',
  ['pek_menu_parser_test.cpp'],
  dependencies : [gtest_main_dep, gtest_dep, pek_menu_parser_dep],
)

config_schema_resources_tests = executable(
  'config_schema_resources_tests',
  ['config_schema_resources_test.cpp'],
  cpp_args : [
    '-DPEK_REPOSITORY_ROOT="@0@"'.format(pek_repository_root),
  ],
  dependencies : [gtest_main_dep, gtest_dep, jsoncons_dep],
)

config_validator_tests = executable(
  'config_validator_tests',
  [
    'config_validator_model_v1_test.cpp',
    'config_validator_opchain_v1_test.cpp',
    '../config-validator/RepositoryValidator.cpp',
    'config_validator_repository_test.cpp',
    'config_validator_report_test.cpp',
    'config_validator_test.cpp',
  ],
  cpp_args : [
    '-DPEK_REPOSITORY_ROOT="@0@"'.format(pek_repository_root),
  ],
  dependencies : [gtest_main_dep, gtest_dep, config_validator_dep, jsoncons_dep],
)

model_descriptor_tests = executable(
  'model_descriptor_tests',
  ['model_descriptor_test.cpp'],
  include_directories : [common_include],
  dependencies : [gtest_main_dep, gtest_dep, pek_deps],
  link_with : [common],
)

base64_tests = executable(
  'base64_tests',
  ['base64_test.cpp'],
  include_directories : [common_include],
  dependencies : [gtest_main_dep, gtest_dep],
  link_with : [common],
)

model_registry_tests = executable(
  'model_registry_tests',
  ['model_registry_test.cpp', '../elements/peksink/model_reg.cpp'],
  include_directories : [common_include, include_directories('../elements/peksink')],
  dependencies : [gtest_main_dep, gtest_dep, pek_deps],
  link_with : [common],
)

performance_tracer_tests = executable(
  'performance_tracer_tests',
  ['performance_tracer_test.cpp'],
  include_directories : [common_include],
  dependencies : [gtest_main_dep, gtest_dep],
  link_with : [common],
)

opchain_descriptor_tests = executable(
  'opchain_descriptor_tests',
  ['opchain_descriptor_test.cpp'],
  include_directories : [common_include],
  dependencies : [gtest_main_dep, gtest_dep, pek_deps],
  link_with : [common],
)

standard_content_type_tests = executable(
  'standard_content_type_tests',
  ['standard_content_type_test.cpp'],
  include_directories : [common_include],
  cpp_args : ['-DPEK_STDOPS_PATH="' + stdops.full_path() + '"'],
  dependencies : [gtest_main_dep, gtest_dep, pek_deps],
  link_with : [common],
)

# Test-only OpChain plugin that simulates inference exceeding one frame budget.
qos_delay_op = shared_module(
  'pek-test-qos-delay',
  ['qos_delay_op.cpp'],
  include_directories : [common_include],
  dependencies : [pek_deps],
  link_with : [common],
  name_prefix : '',
  gnu_symbol_visibility : 'default',
  install : false,
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

performance_metrics_tests = executable(
  'performance_metrics_tests',
  ['performance_metrics_test.cpp'],
  include_directories : [common_include],
  dependencies : [gtest_main_dep, gtest_dep, pek_deps],
  link_with : [common],
)

pektracker_matching_tests = executable(
  'pektracker_matching_tests',
  ['pektracker_matching_test.cpp',
   '../elements/pektracker/Matching.cpp',
   '../elements/pektracker/TrackLifecycle.cpp',
   '../elements/pektracker/TrackState.cpp',
   '../elements/pektracker/Utils.cpp'],
  include_directories : [common_include, include_directories('../elements/pektracker')],
  cpp_args: ['-DFMT_HEADER_ONLY'],
  dependencies : [gtest_main_dep, gtest_dep, pek_deps],
  link_with : [common],
)

pektracker_tracking_output_tests = executable(
  'pektracker_tracking_output_tests',
  ['pektracker_tracking_output_test.cpp',
   '../elements/pektracker/TrackingOutput.cpp'],
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

image_conversion_oracle_test = executable(
  'image_conversion_oracle_test',
  [
    'image_conversion_oracle_test.cpp',
    '../ops-std/preproc/CpuImageKernels.cpp',
    '../ops-std/preproc/CpuImageKernelsI420.cpp',
    '../ops-std/preproc/CpuImageKernelsNv12.cpp',
    '../ops-std/preproc/CpuImageKernelsYuy2.cpp',
  ],
  include_directories : [common_include, include_directories('../ops-std')],
)

python = pek_python
perception_python_sdk_path = meson.project_source_root() / '../generated/perception/python/src'
python_guest_script_test_env = environment()
python_guest_script_test_env.prepend('PYTHONPATH', perception_python_sdk_path)
python_guest_script_executor = executable(
  'python_guest_script_executor',
  ['python_guest_script_executor.cpp'],
  cpp_args : [
    '-DPERCEPTION_PYTHON_EXECUTABLE="' + python.full_path() + '"',
    '-DPERCEPTION_PYTHON_SDK_PATH="' + perception_python_sdk_path + '"',
  ],
  dependencies : [perception_python_bridge_dep],
  install : false,
)

node = find_program('node', required : false)
npm = find_program('npm', required : false)
peksink_web = find_program(meson.project_source_root() / '../scripts/peksink-web.sh')

test('pek_menu_tests', pek_menu_tests)
test('config_schema_resources_tests', config_schema_resources_tests)
test('config_validator_tests', config_validator_tests)
test(
  'pek_config_check_cli_tests',
  python,
  args : [
    files('pek_config_check_cli_test.py'),
    pek_config_check.full_path(),
    pek_repository_root,
  ],
)
test('opchain_descriptor_tests', opchain_descriptor_tests)
test('standard_content_type_tests', standard_content_type_tests, depends : stdops)
test(
  'pek_menu_cli_tests',
  python,
  args : [files('pek_menu_cli_test.py'), pek_menu.full_path()],
  depends : pek_menu,
  is_parallel : false,
)
test(
  'model_artifact_build_tests',
  python,
  args : [files('model_artifact_build_test.py')],
  timeout : 120,
)
test(
  'build_script_tests',
  python,
  args : [files('build_script_test.py'), pek_repository_root],
)
test(
  'setup_lxc_dev_script_tests',
  python,
  args : [files('setup_lxc_dev_script_test.py'), pek_repository_root],
)
test(
  'content_dependency_pipeline_tests',
  python,
  args : [
    files('content_dependency_pipeline_test.py'),
    qos_delay_op.full_path(),
    gstpekinfer.full_path(),
    pekinferpre.full_path(),
  ],
  depends : [gstpekinfer, qos_delay_op, pekinferpre],
)
test(
  'qos_pipeline_tests',
  python,
  args : [
    files('qos_pipeline_test.py'),
    qos_delay_op.full_path(),
    gstpekinfer.full_path(),
    gstpektracker.full_path(),
    gstpekperformance.full_path(),
    gstpekosd.full_path(),
    pekinferpre.full_path(),
  ],
  depends : [
    gstpekinfer,
    qos_delay_op,
    gstpektracker,
    gstpekperformance,
    gstpekosd,
    pekinferpre,
  ],
)
test('model_descriptor_tests', model_descriptor_tests)
test('base64_tests', base64_tests)
test('model_registry_tests', model_registry_tests)
test(
  'opchain_display_metadata_tests',
  python,
  args : [files('opchain_display_metadata_test.py'), pek_repository_root],
)
test('performance_tracer_tests', performance_tracer_tests)
test(
  'release_tool_tests',
  python,
  args : [
    pek_repository_root / 'scripts/release/TestReleaseTool.py',
  ],
)
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
test('performance_metrics_tests', performance_metrics_tests)
test('pektracker_matching_tests', pektracker_matching_tests)
test('pektracker_tracking_output_tests', pektracker_tracking_output_tests)
test('webrtc_session_tests', webrtc_session_tests)
test('runtime_pipeline_tests', runtime_pipeline_tests)
test('image_conversion_oracle_test', image_conversion_oracle_test)
test(
  'python_guest_script_executor_tests',
  python,
  args : [
    files('python_guest_script_executor_test.py'),
    python_guest_script_executor.full_path(),
    files('python_guest/seed_boxes.py'),
    files('python_guest/scale_boxes.py'),
  ],
  depends : python_guest_script_executor,
  env : python_guest_script_test_env,
)

if node.found() and npm.found()
  test(
    'model_ui_tests',
    node,
    args : [
      '--test',
      meson.project_source_root() / 'web/content/models.test.mjs',
    ],
  )
  test(
    'peksink_web_tests',
    peksink_web,
    args : ['test'],
    workdir : meson.project_source_root() / '..',
    timeout : 60,
  )
  test(
    'peksink_web_bundle_check',
    peksink_web,
    args : ['check'],
    workdir : meson.project_source_root() / '..',
  )
endif
