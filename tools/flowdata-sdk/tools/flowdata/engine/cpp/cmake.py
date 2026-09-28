################################################################
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
################################################################

from __future__ import annotations

from pathlib import Path

from ..flatbuffers_compat import cpp_flatbuffers_requirement
from ..python_compat import PYTHON_VERSION_REQUIREMENT
from ..types import GenerationContext, SchemaEntry
from .base import CppIntegrationGenerator


def _write_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_file() and path.read_text(encoding="utf-8") == content:
        return
    path.write_text(content, encoding="utf-8")


class CMakeIntegrationGenerator(CppIntegrationGenerator):
    name = "cmake"

    _template = """\
# Generated file. Do not edit.
# SDK users: change schemas or generator inputs, then regenerate this file.
#
# This file exposes a pre-generated {sdk_name} C++ SDK to CMake consumers.
# Schema changes do NOT trigger regeneration during the build. Re-run your
# project SDK generation script before configuring or rebuilding consumers.
#
# Usage in your CMakeLists.txt:
#
#   include(path/to/generated/cpp/cmake/{sdk_name}.cmake)
#   {sdk_name}_enable_sdk()
#   target_link_libraries(my_app PRIVATE {sdk_name}::sdk)
#
# Overridable variables (set BEFORE including this file):
#   {sdk_name_upper}_FLATBUFFERS_INCLUDE_DIR - FlatBuffers headers (auto-discovered)
#
get_filename_component(_{sdk_name_upper}_SDK_ROOT "${{CMAKE_CURRENT_LIST_DIR}}/../.." ABSOLUTE)
set({sdk_name_upper}_VERSION "{sdk_version}")
set({sdk_name_upper}_FLATBUFFERS_VERSION_REQUIREMENT "{flatbuffers_version_requirement}")
set({sdk_name_upper}_PYTHON_VERSION_REQUIREMENT "{python_version_requirement}")
set({sdk_name_upper}_PYTHON_BRIDGE_AVAILABLE {python_bridge_available})
set({sdk_name_upper}_PYTHON_BRIDGE_MODULE "{python_bridge_module}")

function({sdk_name}_enable_sdk)
  if(TARGET {sdk_name}::sdk)
    message(FATAL_ERROR "{sdk_name}_enable_sdk() has already been called")
  endif()

  if(NOT DEFINED {sdk_name_upper}_FLATBUFFERS_INCLUDE_DIR
      OR {sdk_name_upper}_FLATBUFFERS_INCLUDE_DIR STREQUAL "")
    find_path({sdk_name_upper}_FLATBUFFERS_INCLUDE_DIR flatbuffers/flatbuffers.h REQUIRED)
  endif()

  include(CheckCXXSourceCompiles)
  set(_{sdk_name_upper}_SAVED_REQUIRED_INCLUDES "${{CMAKE_REQUIRED_INCLUDES}}")
  set(CMAKE_REQUIRED_INCLUDES "${{{flatbuffers_include_var}}}")
  check_cxx_source_compiles(
    "#include <flatbuffers/base.h>
     static_assert(FLATBUFFERS_VERSION_MAJOR == {flatbuffers_major});
     static_assert(FLATBUFFERS_VERSION_MINOR == {flatbuffers_minor});
     static_assert(FLATBUFFERS_VERSION_REVISION == {flatbuffers_patch});
     int main() {{ return 0; }}"
    _{sdk_name_upper}_FLATBUFFERS_{flatbuffers_version_cache_key}_MATCHES
  )
  set(CMAKE_REQUIRED_INCLUDES "${{_{sdk_name_upper}_SAVED_REQUIRED_INCLUDES}}")
  if(NOT _{sdk_name_upper}_FLATBUFFERS_{flatbuffers_version_cache_key}_MATCHES)
    message(FATAL_ERROR
      "{sdk_name} requires FlatBuffers {flatbuffers_version}; "
      "headers under '${{{flatbuffers_include_var}}}' are incompatible")
  endif()

  add_library(_{sdk_name}_sdk INTERFACE)
  add_library({sdk_name}::sdk ALIAS _{sdk_name}_sdk)

  target_compile_features(_{sdk_name}_sdk INTERFACE cxx_std_20)
  target_include_directories(_{sdk_name}_sdk INTERFACE
    "${{{sdk_root_var}}}/cpp"
    "${{{flatbuffers_include_var}}}"
  )
endfunction()

{python_bridge_block}
"""

    def write_outputs(self, context: GenerationContext, entries: list[SchemaEntry]) -> list[Path]:
        del entries
        cmake_module_path = context.cpp_root / "cmake" / f"{context.effective_public_name}.cmake"
        _write_text(
            cmake_module_path,
            self._template.format(
                sdk_name=context.effective_public_name,
                sdk_name_upper=context.effective_public_name.upper(),
                sdk_version=str(context.sdk_version),
                flatbuffers_version_requirement=cpp_flatbuffers_requirement(
                    context.flatc_version
                ),
                flatbuffers_version=str(context.flatc_version),
                flatbuffers_major=context.flatc_version.major,
                flatbuffers_minor=context.flatc_version.minor,
                flatbuffers_patch=context.flatc_version.patch,
                flatbuffers_version_cache_key=str(context.flatc_version).replace(".", "_"),
                python_version_requirement=PYTHON_VERSION_REQUIREMENT,
                python_bridge_available="TRUE" if context.cpp_python_bridge else "FALSE",
                python_bridge_module=(
                    f"{context.effective_public_name}_bridge" if context.cpp_python_bridge else ""
                ),
                sdk_root_var=f"_{context.effective_public_name.upper()}_SDK_ROOT",
                flatbuffers_include_var=f"{context.effective_public_name.upper()}_FLATBUFFERS_INCLUDE_DIR",
                python_bridge_block=_python_bridge_block(context.effective_public_name, context.effective_public_name)
                if context.cpp_python_bridge
                else "",
            ),
        )
        return [cmake_module_path]


def _python_bridge_block(sdk_name: str, bridge_source_name: str) -> str:
    sdk_name_upper = sdk_name.upper()
    return f"""\
function({sdk_name}_enable_python_bridge target)
  if(NOT TARGET ${{target}})
    message(FATAL_ERROR "{sdk_name}_enable_python_bridge() target not found: ${{target}}")
  endif()

  if(NOT TARGET {sdk_name}::sdk)
    {sdk_name}_enable_sdk()
  endif()

  find_package(Python3 3.10 COMPONENTS Development REQUIRED)

  set(_{sdk_name_upper}_PYTHON_BRIDGE_SOURCE
    "${{_{sdk_name_upper}_SDK_ROOT}}/cpp/python_bridge/{bridge_source_name}_python_bridge.cpp")
  if(NOT EXISTS "${{_{sdk_name_upper}_PYTHON_BRIDGE_SOURCE}}")
    message(FATAL_ERROR "{sdk_name} Python bridge was not generated")
  endif()

  target_sources(${{target}} PRIVATE "${{_{sdk_name_upper}_PYTHON_BRIDGE_SOURCE}}")
  target_link_libraries(${{target}} PRIVATE {sdk_name}::sdk Python3::Python)
endfunction()
"""
