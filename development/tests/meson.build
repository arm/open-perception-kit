# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


jsoncons_dep = dependency(
  'jsoncons',
  required : true,
  fallback : ['jsoncons', 'jsoncons_dep'],
)

pipeline_preset_tests = executable(
  'pipeline_preset_tests',
  ['pipeline_preset_test.cpp'],
  include_directories : [common_include],
  dependencies : [gtest_main_dep, gtest_dep, fmt_dep, magic_enum_dep, tl_expected_dep],
  link_with : [common],
)

config_schema_resources_tests = executable(
  'config_schema_resources_tests',
  ['config_schema_resources_test.cpp'],
  cpp_args : [
    '-DOPK_REPOSITORY_ROOT="@0@"'.format(opk_repository_root),
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
    '-DOPK_REPOSITORY_ROOT="@0@"'.format(opk_repository_root),
  ],
  dependencies : [gtest_main_dep, gtest_dep, config_validator_dep, jsoncons_dep],
)

model_descriptor_tests = executable(
  'model_descriptor_tests',
  ['model_descriptor_test.cpp'],
  include_directories : [common_include],
  dependencies : [gtest_main_dep, gtest_dep, opk_deps],
  link_with : [common],
)

base64_tests = executable(
  'base64_tests',
  ['base64_test.cpp'],
  include_directories : [common_include],
  dependencies : [gtest_main_dep, gtest_dep],
  link_with : [common],
)

surface_painter_tests = executable(
  'surface_painter_tests',
  ['surface_painter_test.cpp', '../elements/opkosd/DebugOverlayRenderer.cpp'],
  include_directories : [common_include, include_directories('../elements/opkosd')],
  dependencies : [gtest_main_dep, gtest_dep, opk_deps],
  link_with : [common],
)

gst_meta_wrapper_tests = executable(
  'gst_meta_wrapper_tests',
  ['gst_meta_wrapper_test.cpp'],
  include_directories : [common_include],

  dependencies : [gtest_main_dep, gtest_dep, opk_deps],
  link_with : [common],
)

model_registry_tests = executable(
  'model_registry_tests',
  ['model_registry_test.cpp', '../elements/opksink/model_reg.cpp'],
  include_directories : [common_include, include_directories('../elements/opksink')],
  dependencies : [gtest_main_dep, gtest_dep, opk_deps],
  link_with : [common],
)

opchain_descriptor_tests = executable(
  'opchain_descriptor_tests',
  ['opchain_descriptor_test.cpp'],
  include_directories : [common_include],
  dependencies : [gtest_main_dep, gtest_dep, opk_deps],
  link_with : [common],
)

opk_menu_discover_tests = executable(
  'opk_menu_discover_tests',
  ['opk_menu_discover_test.cpp', '../opk-menu/discover.cpp'],
  include_directories : [common_include, include_directories('../opk-menu')],
  dependencies : [gtest_main_dep, gtest_dep, opk_deps],
  link_with : [common],
)

standard_content_type_tests = executable(
  'standard_content_type_tests',
  ['standard_content_type_test.cpp'],
  include_directories : [common_include],
  cpp_args : ['-DOPK_STDOPS_PATH="' + stdops.full_path() + '"'],
  dependencies : [gtest_main_dep, gtest_dep, opk_deps],
  link_with : [common],
)

if python_ops_enabled
  python_script_op_tests = executable(
    'python_script_op_tests',
    ['python_script_op_test.cpp', '../ops-python/Plugin.cpp'],
    include_directories : [common_include, include_directories('../ops-python')],
    cpp_args : [
      '-DPYTHON_SCRIPT_OP_FIXTURES="' + meson.current_source_dir() / 'python_script_op' + '"',
    ],
    dependencies : [gtest_main_dep, gtest_dep, python_ops_test_dep],
  )
  python_runtime_attachment_test_module = shared_module(
    'opk_python_runtime_attachment_test',
    ['python_runtime_attachment_test_module.cpp'],
    include_directories : [common_include, include_directories('../ops-python')],
    cpp_args : [
      '-DPYTHON_SCRIPT_OP_FIXTURES="' + meson.current_source_dir() / 'python_script_op' + '"',
    ],
    dependencies : [python_ops_test_dep],
    name_prefix : '',
  )
  byom_blazeface_example_test_env = environment()
  byom_blazeface_example_test_env.prepend(
    'PYTHONPATH',
    meson.project_source_root() / '../generated/open_perception_kit/python/src',
  )
  test(
    'byom_blazeface_example_tests',
    test_python,
    args : [files('byom_blazeface_example_test.py')],
    env : byom_blazeface_example_test_env,
  )
  python_script_pipeline_test_env = environment()
  python_script_pipeline_test_env.prepend(
    'PYTHONPATH',
    meson.project_source_root() / '../generated/open_perception_kit/python/src',
  )
  test(
    'python_script_pipeline_tests',
    test_python,
    args : [
      files('python_script_pipeline_test.py'),
      meson.project_source_root() / '..',
      meson.project_build_root() / 'meson-out',
    ],
    env : python_script_pipeline_test_env,
    depends : [gstopkinfer, opkcomm, onnxops, stdops, python_ops],
    timeout : 90,
    is_parallel : false,
  )
endif

# Test-only OpChain plugin that simulates inference exceeding one frame budget.
qos_delay_op = shared_module(
  'opk-test-qos-delay',
  ['qos_delay_op.cpp'],
  include_directories : [common_include],
  dependencies : [opk_deps],
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
  dependencies : [gtest_main_dep, gtest_dep, opk_deps],
  link_with : [common],
)

gaze_detection_parser_tests = executable(
  'gaze_detection_parser_tests',
  ['gaze_detection_parser_test.cpp', '../ops-std/postproc/GazeDetectionParser.cpp'],
  include_directories : [common_include, include_directories('../ops-std')],
  cpp_args: ['-DFMT_HEADER_ONLY'],
  dependencies : [gtest_main_dep, gtest_dep, opk_deps],
  link_with : [common],
)

onnx_tensor_tests = executable(
  'onnx_tensor_tests',
  ['onnx_tensor_test.cpp'],
  include_directories : [common_include, include_directories('../ops-onnx')],
  dependencies : [gtest_main_dep, gtest_dep, opk_deps, onnx_dep],
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
  dependencies : [gtest_main_dep, gtest_dep, opk_deps],
  link_with : [common],
)

opktracker_matching_tests = executable(
  'opktracker_matching_tests',
  ['opktracker_matching_test.cpp',
   '../elements/opktracker/Matching.cpp',
   '../elements/opktracker/TrackLifecycle.cpp',
   '../elements/opktracker/TrackState.cpp',
   '../elements/opktracker/Utils.cpp'],
  include_directories : [common_include, include_directories('../elements/opktracker')],
  cpp_args: ['-DFMT_HEADER_ONLY'],
  dependencies : [gtest_main_dep, gtest_dep, opk_deps],
  link_with : [common],
)

opktracker_tracking_output_tests = executable(
  'opktracker_tracking_output_tests',
  ['opktracker_tracking_output_test.cpp',
   '../elements/opktracker/TrackingOutput.cpp'],
  include_directories : [common_include, include_directories('../elements/opktracker')],
  cpp_args: ['-DFMT_HEADER_ONLY'],
  dependencies : [gtest_main_dep, gtest_dep, opk_deps],
  link_with : [common],
)

webrtc_session_tests = executable(
  'webrtc_session_tests',
  ['webrtc_session_test.cpp',
   '../elements/opksink/webrtc_session.cpp',
   '../elements/opksink/utils.cpp'],
  include_directories : [common_include, include_directories('../elements/opksink')],
  dependencies : [gtest_main_dep, gtest_dep, opk_deps],
  link_with : [common],
)

runtime_pipeline_tests = executable(
  'runtime_pipeline_tests',
  ['runtime_pipeline_test.cpp'],
  include_directories : [common_include, include_directories('..')],
  dependencies : [gtest_main_dep, gtest_dep, opk_deps],
  link_with : [opk_runtime, common],
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

python = test_python
# The distro GStreamer bindings use the distro Python ABI, not the Python Ops venv.
gst_python = import('python').find_installation('/usr/bin/python3', modules : ['gi'])
perception_python_sdk_path = meson.project_source_root() / '../generated/open_perception_kit/python/src'
python_guest_script_test_env = environment()
python_guest_script_test_env.prepend('PYTHONPATH', perception_python_sdk_path)
python_guest_script_executor = executable(
  'python_guest_script_executor',
  ['python_guest_script_executor.cpp'],
  cpp_args : [
    '-DPERCEPTION_PYTHON_EXECUTABLE="' + opk_python.full_path() + '"',
    '-DPERCEPTION_PYTHON_SDK_PATH="' + perception_python_sdk_path + '"',
  ],
  dependencies : [open_perception_kit_python_bridge_dep],
  install : false,
)

node = find_program('node', required : false)
npm = find_program('npm', required : false)
opksink_web = find_program(meson.project_source_root() / '../scripts/opksink-web.sh')

test('pipeline_preset_tests', pipeline_preset_tests)
test('config_schema_resources_tests', config_schema_resources_tests)
test('config_validator_tests', config_validator_tests)
test(
  'opk_config_check_cli_tests',
  python,
  args : [
    files('opk_config_check_cli_test.py'),
    opk_config_check.full_path(),
    opk_repository_root,
  ],
)
test('opchain_descriptor_tests', opchain_descriptor_tests)
test('opk_menu_discover_tests', opk_menu_discover_tests)
test('standard_content_type_tests', standard_content_type_tests, depends : stdops)
if python_ops_enabled
  test('python_script_op_tests', python_script_op_tests)
  test(
    'python_runtime_attachment_tests',
    opk_python,
    args : [
      files('python_runtime_attachment_test.py'),
      python_runtime_attachment_test_module.full_path(),
    ],
    depends : python_runtime_attachment_test_module,
  )
endif
test(
  'opk_menu_cli_tests',
  python,
  args : [files('opk_menu_cli_test.py'), opk_menu.full_path()],
  depends : opk_menu,
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
  args : [files('build_script_test.py'), opk_repository_root],
  timeout : 60,
)
test(
  'clang_tidy_report_tests',
  python,
  args : [files('clang_tidy_report_test.py')],
  timeout : 120,
)
test(
  'setup_lxc_dev_script_tests',
  python,
  args : [files('setup_lxc_dev_script_test.py'), opk_repository_root],
)
test(
  'content_dependency_pipeline_tests',
  gst_python,
  args : [
    files('content_dependency_pipeline_test.py'),
    qos_delay_op.full_path(),
    gstopkinfer.full_path(),
    opkinferpre.full_path(),
  ],
  depends : [gstopkinfer, qos_delay_op, opkinferpre],
)
test(
  'qos_pipeline_tests',
  gst_python,
  args : [
    files('qos_pipeline_test.py'),
    qos_delay_op.full_path(),
    gstopkinfer.full_path(),
    gstopktracker.full_path(),
    gstopkperformance.full_path(),
    gstopkosd.full_path(),
    opkinferpre.full_path(),
  ],
  depends : [
    gstopkinfer,
    qos_delay_op,
    gstopktracker,
    gstopkperformance,
    gstopkosd,
    opkinferpre,
  ],
)
test('model_descriptor_tests', model_descriptor_tests)
test('base64_tests', base64_tests)
test('surface_painter_tests', surface_painter_tests)
test('gst_meta_wrapper_tests', gst_meta_wrapper_tests)
test('model_registry_tests', model_registry_tests)
test(
  'opchain_display_metadata_tests',
  python,
  args : [files('opchain_display_metadata_test.py'), opk_repository_root],
)
test(
  'release_tool_tests',
  python,
  args : [
    opk_repository_root / 'scripts/release/TestReleaseTool.py',
  ],
  timeout : 60,
)
test('matrix_operations_tests', matrix_operations_tests)
test('kalman_filter_tests', kalman_filter_tests)
test('hungarian_tests', hungarian_tests)
test('iou_tests', iou_tests)
test('yolo_parser_tests', yolo_parser_tests)
test('gaze_detection_parser_tests', gaze_detection_parser_tests)
test('onnx_tensor_tests', onnx_tensor_tests)
test('logging_tests', logging_tests)
test('logger_tests', logger_tests)
test(
  'logging_environment_tests',
  python,
  args : [files('logging_environment_test.py'), logging_environment_probe.full_path()],
  depends : logging_environment_probe,
)
test('performance_metrics_tests', performance_metrics_tests)
test('opktracker_matching_tests', opktracker_matching_tests)
test('opktracker_tracking_output_tests', opktracker_tracking_output_tests)
test('webrtc_session_tests', webrtc_session_tests)
foreach endpoint : ['control', 'webrtc']
  test(
    'opksink_websocket_json_' + endpoint,
    gst_python,
    args : [files('opksink_websocket_json_test.py'), opkinferpre.full_path(), endpoint],
    depends : opkinferpre,
    is_parallel : false,
    timeout : 20,
  )
endforeach
test(
  'opksink_configured_ports',
  gst_python,
  args : [files('opksink_configured_ports_test.py'), opkinferpre.full_path()],
  depends : opkinferpre,
)
test(
  'websocket_shutdown',
  gst_python,
  args : [files('websocket_shutdown_test.py'), opkinferpre.full_path(), opkcomm.full_path()],
  depends : [opkinferpre, opkcomm],
  env : ['G_DEBUG=fatal-criticals'],
)
test(
  'opkcomm_file_boundary',
  gst_python,
  args : [files('opkcomm_file_boundary_test.py'), opkcomm.full_path()],
  depends : opkcomm,
  is_parallel : false,
  timeout : 30,
)
foreach failed_element : [
  'vconv',
  'rtp_tee',
  'drain_fakesink',
  'audio_silence_src',
  'audio_tee',
  'audio_drain_fakesink',
]
  test(
    'opksink_factory_failure_' + failed_element,
    opksink_factory_failure_test,
    args : [opkinferpre.full_path(), 'factory', failed_element],
    depends : opkinferpre,
    is_parallel : false,
  )
endforeach
foreach failed_operation : [
  'add_vp8enc',
  'add_aconv',
  'pad_link_video_drain',
  'create_video_ghost_pad',
  'pad_link_audio_real',
  'pad_link_audio_drain',
]
  test(
    'opksink_operation_failure_' + failed_operation,
    opksink_factory_failure_test,
    args : [opkinferpre.full_path(), 'operation', failed_operation],
    depends : opkinferpre,
    is_parallel : false,
  )
endforeach
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
    'opksink_web_tests',
    opksink_web,
    args : ['test'],
    workdir : meson.project_source_root() / '..',
    timeout : 60,
  )
  test(
    'opksink_web_bundle_check',
    opksink_web,
    args : ['check'],
    workdir : meson.project_source_root() / '..',
  )
endif
