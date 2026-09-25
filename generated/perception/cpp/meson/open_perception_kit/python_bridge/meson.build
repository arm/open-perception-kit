# Generated file. Do not edit.
# SDK users: change schemas or generator inputs, then regenerate this file.
#
# Include the parent open_perception_kit Meson integration before this directory.

_open_perception_kit_python = import('python').find_installation()
_open_perception_kit_python_dep = _open_perception_kit_python.dependency(embed : true, required : true)
_open_perception_kit_python_bridge_sources = files('../../../python_bridge/perception_python_bridge.cpp')

open_perception_kit_python_bridge_dep = declare_dependency(
  include_directories : [_open_perception_kit_inc],
  compile_args        : _open_perception_kit_cpp20_args,
  dependencies        : [open_perception_kit_dep, _open_perception_kit_python_dep],
  sources             : _open_perception_kit_python_bridge_sources,
)
