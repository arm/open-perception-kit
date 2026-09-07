# Generated file. Do not edit.
# SDK users: change schemas or generator inputs, then regenerate this file.
#
# Provides the pre-generated perception C++ SDK as a Meson dependency.
# Schema changes do NOT trigger regeneration during the build. Re-run your
# project SDK generation script before configuring or rebuilding consumers.
#
# Usage:
#   subdir('perception')
#   executable('app', 'app.cpp', dependencies: [perception_dep])
#
# Embedded Python bridge usage, when generated:
#   subdir('perception/python_bridge')
#   executable('host', 'host.cpp', dependencies: [perception_python_bridge_dep])

_perception_cpp = meson.get_compiler('cpp')
perception_version = '0.3.0'
perception_flatbuffers_version_requirement = '==25.9.23'
perception_python_version_requirement = '>=3.10'
perception_python_bridge_available = true
perception_python_bridge_module = 'perception_bridge'

# ── FlatBuffers ───────────────────────────────────────────────────────────
# partial_dependency strips all link/rpath info — only include dirs and
# compile args propagate to consumers. FlatBuffers is header-only in this
# integration.
_perception_flatbuffers_dep_full = dependency(
  'flatbuffers',
  required: false,
  version: perception_flatbuffers_version_requirement,
)
_perception_extra_deps = []
if _perception_flatbuffers_dep_full.found()
  _perception_extra_deps += [
    _perception_flatbuffers_dep_full.partial_dependency(
      compile_args : true,
      includes     : true,
    )
  ]
else
  _perception_flatbuffers_version_check = '''
    #include <flatbuffers/base.h>
    static_assert(FLATBUFFERS_VERSION_MAJOR == 25);
    static_assert(FLATBUFFERS_VERSION_MINOR == 9);
    static_assert(FLATBUFFERS_VERSION_REVISION == 23);
    int main() { return 0; }
  '''
  if not _perception_cpp.compiles(
    _perception_flatbuffers_version_check,
    name : 'perception FlatBuffers 25.9.23 compatibility',
  )
    error('perception requires FlatBuffers 25.9.23 headers')
  endif
endif

# ── C++20 requirement ─────────────────────────────────────────────────────
_perception_cpp20_args = _perception_cpp.get_supported_arguments('-std=c++20')
if _perception_cpp20_args.length() == 0
  _perception_cpp20_args = _perception_cpp.get_supported_arguments('/std:c++20')
endif
if _perception_cpp20_args.length() == 0
  warning('perception: compiler does not accept -std=c++20 or /std:c++20; '
          + 'ensure your project sets cpp_std >= c++20 in default_options')
endif

# ── Public dependency ─────────────────────────────────────────────────────
# Generated headers live under the pre-generated SDK root.
_perception_inc = include_directories('../..')

perception_dep = declare_dependency(
  include_directories : [_perception_inc],
  compile_args        : _perception_cpp20_args,
  dependencies        : _perception_extra_deps,
)
