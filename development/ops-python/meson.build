python_embed_dep = pek_python.dependency(embed : true, required : true)
python_runtime_path = ''
if not release_package
  python_runtime_path = meson.project_source_root() / '../generated/perception/python/src'
endif

python_ops_sources = [
  'PythonRuntime.cpp',
  'PythonScriptOp.cpp',
  'TensorBridge.cpp',
]

python_executable = pek_python.full_path()
if release_package
  python_executable = '/usr/bin/python3'
endif

python_ops_cpp_args = [
  '-DPEK_PYTHON_EXECUTABLE="' + python_executable + '"',
  '-DPEK_DEVELOPMENT_PYTHON_PATH="' + python_runtime_path + '"',
]

python_ops = shared_module(
  'pek-python-ops',
  sources : python_ops_sources + ['Plugin.cpp'],
  include_directories : [common_include, include_directories(pek_numpy_include)],
  dependencies : [pek_deps, perception_python_bridge_dep, python_embed_dep],
  link_with : [common],
  cpp_args : python_ops_cpp_args,
  name_prefix : '',
  gnu_symbol_visibility : 'default',
  install : release_package,
  install_dir : 'lib/pek',
  install_rpath : '$ORIGIN',
)

python_ops_test_dep = declare_dependency(
  include_directories : [include_directories('.'), include_directories(pek_numpy_include)],
  dependencies : [pek_deps, perception_python_bridge_dep, python_embed_dep],
  link_with : [common],
  sources : python_ops_sources,
  compile_args : python_ops_cpp_args,
)

if release_package
  install_data(
    'pek_python_ops.pyi',
    install_dir : 'share/pek/python',
  )
endif
