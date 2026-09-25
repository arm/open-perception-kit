################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from pathlib import Path

from ..flatbuffers_compat import cpp_flatbuffers_requirement
from ..python_compat import PYTHON_VERSION_REQUIREMENT
from ..types import GenerationContext, SchemaEntry
from .base import CppIntegrationGenerator


def _meson_str(value: Path | str) -> str:
    return str(value).replace("\\", "/").replace("'", "\\'")


def _write_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_file() and path.read_text(encoding="utf-8") == content:
        return
    path.write_text(content, encoding="utf-8")


class MesonIntegrationGenerator(CppIntegrationGenerator):
    name = "meson"

    _template = """\
# Generated file. Do not edit.
# SDK users: change schemas or generator inputs, then regenerate this file.
#
# Provides the pre-generated {sdk_name} C++ SDK as a Meson dependency.
# Schema changes do NOT trigger regeneration during the build. Re-run your
# project SDK generation script before configuring or rebuilding consumers.
#
# Usage:
#   subdir('{sdk_name}')
#   executable('app', 'app.cpp', dependencies: [{sdk_name}_dep])
#
# Embedded Python bridge usage, when generated:
#   subdir('{sdk_name}/python_bridge')
#   executable('host', 'host.cpp', dependencies: [{sdk_name}_python_bridge_dep])

_{sdk_name}_cpp = meson.get_compiler('cpp')
{sdk_name}_version = '{sdk_version}'
{sdk_name}_flatbuffers_version_requirement = '{flatbuffers_version_requirement}'
{sdk_name}_python_version_requirement = '{python_version_requirement}'
{sdk_name}_python_bridge_available = {python_bridge_available}
{sdk_name}_python_bridge_module = '{python_bridge_module}'

# ── FlatBuffers ───────────────────────────────────────────────────────────
# partial_dependency strips all link/rpath info — only include dirs and
# compile args propagate to consumers. FlatBuffers is header-only in this
# integration.
_{sdk_name}_flatbuffers_dep_full = dependency(
  'flatbuffers',
  required: false,
  version: {sdk_name}_flatbuffers_version_requirement,
)
_{sdk_name}_extra_deps = []
if _{sdk_name}_flatbuffers_dep_full.found()
  _{sdk_name}_extra_deps += [
    _{sdk_name}_flatbuffers_dep_full.partial_dependency(
      compile_args : true,
      includes     : true,
    )
  ]
else
  _{sdk_name}_flatbuffers_version_check = '''
    #include <flatbuffers/base.h>
    static_assert(FLATBUFFERS_VERSION_MAJOR == {flatbuffers_major});
    static_assert(FLATBUFFERS_VERSION_MINOR == {flatbuffers_minor});
    static_assert(FLATBUFFERS_VERSION_REVISION == {flatbuffers_patch});
    int main() {{ return 0; }}
  '''
  if not _{sdk_name}_cpp.compiles(
    _{sdk_name}_flatbuffers_version_check,
    name : '{sdk_name} FlatBuffers {flatbuffers_version} compatibility',
  )
    error('{sdk_name} requires FlatBuffers {flatbuffers_version} headers')
  endif
endif

# ── C++20 requirement ─────────────────────────────────────────────────────
_{sdk_name}_cpp20_args = _{sdk_name}_cpp.get_supported_arguments('-std=c++20')
if _{sdk_name}_cpp20_args.length() == 0
  _{sdk_name}_cpp20_args = _{sdk_name}_cpp.get_supported_arguments('/std:c++20')
endif
if _{sdk_name}_cpp20_args.length() == 0
  warning('{sdk_name}: compiler does not accept -std=c++20 or /std:c++20; '
          + 'ensure your project sets cpp_std >= c++20 in default_options')
endif

# ── Public dependency ─────────────────────────────────────────────────────
# Generated headers live under the pre-generated SDK root.
_{sdk_name}_inc = include_directories('{rel_cpp_root}')

{sdk_name}_dep = declare_dependency(
  include_directories : [_{sdk_name}_inc],
  compile_args        : _{sdk_name}_cpp20_args,
  dependencies        : _{sdk_name}_extra_deps,
)

"""

    _python_bridge_template = """\
# Generated file. Do not edit.
# SDK users: change schemas or generator inputs, then regenerate this file.
#
# Include the parent {sdk_name} Meson integration before this directory.

_{sdk_name}_python = import('python').find_installation()
_{sdk_name}_python_dep = _{sdk_name}_python.dependency(embed : true, required : true)
_{sdk_name}_python_bridge_sources = files('{rel_cpp_root}/python_bridge/{bridge_source_name}_python_bridge.cpp')

{sdk_name}_python_bridge_dep = declare_dependency(
  include_directories : [_{sdk_name}_inc],
  compile_args        : _{sdk_name}_cpp20_args,
  dependencies        : [{sdk_name}_dep, _{sdk_name}_python_dep],
  sources             : _{sdk_name}_python_bridge_sources,
)
"""

    def write_outputs(self, context: GenerationContext, entries: list[SchemaEntry]) -> list[Path]:
        del entries
        meson_module = context.cpp_root / "meson" / context.effective_public_name / "meson.build"
        rel_cpp_root = "../.."

        content = self._template.format(
            sdk_name=context.effective_public_name,
            sdk_version=str(context.sdk_version),
            flatbuffers_version_requirement=cpp_flatbuffers_requirement(
                context.flatc_version
            ),
            flatbuffers_version=str(context.flatc_version),
            flatbuffers_major=context.flatc_version.major,
            flatbuffers_minor=context.flatc_version.minor,
            flatbuffers_patch=context.flatc_version.patch,
            python_version_requirement=PYTHON_VERSION_REQUIREMENT,
            python_bridge_available="true" if context.cpp_python_bridge else "false",
            python_bridge_module=(
                f"{context.sdk_name}_bridge" if context.cpp_python_bridge else ""
            ),
            rel_cpp_root=rel_cpp_root,
        )
        _write_text(meson_module, content)
        outputs = [meson_module]
        if context.cpp_python_bridge:
            bridge_module = meson_module.parent / "python_bridge" / "meson.build"
            _write_text(
                bridge_module,
                self._python_bridge_template.format(
                    sdk_name=context.effective_public_name,
                    bridge_source_name=context.sdk_name,
                    rel_cpp_root="../../..",
                ),
            )
            outputs.append(bridge_module)
        return outputs
