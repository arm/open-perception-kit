# Generated project adapter. Do not edit.
# Regenerate with ./scripts/perception-sdk.sh generate.

open_perception_kit_version = '0.1.0'
open_perception_kit_flatbuffers_version_requirement = '==25.9.23'

_open_perception_kit_flatbuffers_dep = dependency(
  'flatbuffers',
  required : true,
  version : open_perception_kit_flatbuffers_version_requirement,
)
_open_perception_kit_inc = include_directories('../../generated/open_perception_kit/cpp')

open_perception_kit_dep = declare_dependency(
  include_directories : [_open_perception_kit_inc],
  compile_args : ['-std=c++20'],
  dependencies : [_open_perception_kit_flatbuffers_dep],
)

if get_option('tests') or python_ops_enabled
  _open_perception_kit_python = opk_python
  _open_perception_kit_python_embed_dep = _open_perception_kit_python.dependency(
    embed : true,
    required : true,
  )
  _open_perception_kit_python_bridge_sources = files(
    '../../generated/open_perception_kit/cpp/python_bridge/open_perception_kit_python_bridge.cpp',
  )

  open_perception_kit_python_bridge_dep = declare_dependency(
    include_directories : [_open_perception_kit_inc],
    compile_args : ['-std=c++20'],
    dependencies : [open_perception_kit_dep, _open_perception_kit_python_embed_dep],
    sources : _open_perception_kit_python_bridge_sources,
  )
endif
