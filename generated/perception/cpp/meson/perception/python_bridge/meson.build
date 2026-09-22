# Generated file. Do not edit.
# SDK users: change schemas or generator inputs, then regenerate this file.
#
# Include the parent perception Meson integration before this directory.

_perception_python = import('python').find_installation()
_perception_python_dep = _perception_python.dependency(embed : true, required : true)
_perception_python_bridge_sources = files('../../../python_bridge/perception_python_bridge.cpp')

perception_python_bridge_dep = declare_dependency(
  include_directories : [_perception_inc],
  compile_args        : _perception_cpp20_args,
  dependencies        : [perception_dep, _perception_python_dep],
  sources             : _perception_python_bridge_sources,
)
