
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
  link_with : [common, model_loading],
)

# Tests retain production's real header-only C++ projection and replace only
# its C ABI backend. The dependency-boundary test below keeps this fake exact.
fake_modelfetch = shared_module(
  'fake_modelfetch',
  ['fake_modelfetch.c'],
  dependencies : [modelfetch_cpp_headers_dep],
  install : false,
)

pekinfer_blocking_setup_op = shared_module(
  'pek-test-blocking-setup',
  ['pekinfer_blocking_setup_op.cpp'],
  include_directories : [common_include],
  dependencies : [pek_deps],
  link_with : [common, model_loading],
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

shape_tests = executable(
  'shape_tests',
  ['shape_test.cpp'],
  include_directories : [common_include],
  dependencies : [gtest_main_dep, gtest_dep, nlohmann_dep],
)

onnx_tensor_tests = executable(
  'onnx_tensor_tests',
  ['onnx_tensor_test.cpp'],
  include_directories : [
    common_include,
    include_directories('../ops-onnx'),
  ],
  dependencies : [gtest_main_dep, gtest_dep, pek_deps, onnx_dep],
  link_with : [common],
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

if is_variable('executorchops')
  executorch_inference_tests = executable(
    'executorch_inference_tests',
    ['executorch_inference_test.cpp', '../ops-executorch/Inference.cpp'],
    include_directories : [
      common_include,
      include_directories('../ops-executorch'),
    ],
    cpp_args : ['-DFMT_HEADER_ONLY'],
    dependencies : [gtest_main_dep, gtest_dep, pek_deps, executorch_runtime_dep],
    link_with : [common],
  )
endif

node = find_program('node', required : false)
python = import('python').find_installation()
gst_launch = find_program('gst-launch-1.0')
readelf = find_program('readelf')

model_consumers = [onnxops]
if is_variable('executorchops')
  model_consumers += [executorchops]
endif
if is_variable('hailortops')
  model_consumers += [hailortops]
endif
if is_variable('ncnnops')
  model_consumers += [ncnnops]
endif

model_consumer_args = ['--model-consumer']
foreach model_consumer : model_consumers
  model_consumer_args += [model_consumer.full_path()]
endforeach

test('pek_menu_tests', pek_menu_tests)
test(
  'model_descriptor_tests',
  model_descriptor_tests,
  env : ['LD_PRELOAD=' + fake_modelfetch.full_path()],
  depends : fake_modelfetch,
)
test(
  'pek_menu_cli_tests',
  python,
  args : [files('pek_menu_cli_test.py'), pek_menu.full_path()],
  depends : pek_menu,
)
test(
  'pekinfer_lazy_setup_tests',
  python,
  args : [
    files('pekinfer_lazy_setup_test.py'),
    gst_launch,
    gstpekinfer.full_path(),
    pekinfer_blocking_setup_op.full_path(),
    fake_modelfetch.full_path(),
  ],
  depends : [gstpekinfer, pekinfer_blocking_setup_op, fake_modelfetch],
)
test(
  'prepare_modelfetch_release_tests',
  python,
  args : [
    meson.project_source_root() / '../scripts/private/tests/test_prepare_modelfetch_release.py',
  ],
)
test(
  'modelfetch_dependency_boundary_tests',
  python,
  args : [
    files('modelfetch_dependency_boundary_test.py'),
    readelf,
    '--non-model-consumer',
    common.full_path(),
    stdops.full_path(),
    gstpektracker.full_path(),
    gstpekperformance.full_path(),
    gstpekosd.full_path(),
    pekinferpre.full_path(),
    pekcomm.full_path(),
    pek_menu.full_path(),
    gstpekinfer.full_path(),
    pek_runtime.full_path(),
    '--model-runtime',
    model_loading.full_path(),
    '--fake-backend',
    fake_modelfetch.full_path(),
  ] + model_consumer_args,
  depends : [
    common,
    model_loading,
    stdops,
    gstpekinfer,
    gstpektracker,
    gstpekperformance,
    gstpekosd,
    pekinferpre,
    pekcomm,
    pek_menu,
    pek_runtime,
    fake_modelfetch,
  ] + model_consumers,
)
test('matrix_operations_tests', matrix_operations_tests)
test('shape_tests', shape_tests)
test('onnx_tensor_tests', onnx_tensor_tests)
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
if is_variable('executorch_inference_tests')
  test(
    'executorch_inference_tests',
    executorch_inference_tests,
    env : [
      'PEK_TEST_EXECUTORCH_MODEL=' + (
        meson.project_source_root() / '../config/models/yolox/yolox_nano.pte'
      ),
    ],
  )
endif

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
