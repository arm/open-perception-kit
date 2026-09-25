# Generated project adapter. Do not edit.
# Regenerate with ./scripts/perception-sdk.sh generate.

perception_version = '0.1.0'
perception_flatbuffers_version_requirement = '==25.9.23'

_perception_flatbuffers_dep = dependency(
  'flatbuffers',
  required : true,
  version : perception_flatbuffers_version_requirement,
)
_perception_inc = include_directories('../../generated/perception/cpp')

perception_dep = declare_dependency(
  include_directories : [_perception_inc],
  compile_args : ['-std=c++20'],
  dependencies : [_perception_flatbuffers_dep],
)

if get_option('tests') or python_ops_enabled
  _perception_python = opk_python
  _perception_python_embed_dep = _perception_python.dependency(
    embed : true,
    required : true,
  )
  _perception_python_bridge_sources = files(
    '../../generated/perception/cpp/python_bridge/perception_python_bridge.cpp',
  )

  perception_python_bridge_dep = declare_dependency(
    include_directories : [_perception_inc],
    compile_args : ['-std=c++20'],
    dependencies : [perception_dep, _perception_python_embed_dep],
    sources : _perception_python_bridge_sources,
  )
endif
