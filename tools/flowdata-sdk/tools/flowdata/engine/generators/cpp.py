# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

from __future__ import annotations

from ..cpp.cpp_sdk_codegen import generate_flatbuffers_cpp, generate_header
from ..cpp.python_bridge import generate_python_bridge
from ..schema_set import schema_set_sha256
from ..types import GenerationContext, SchemaEntry
from .base import SdkGenerator


class CppSdkGenerator(SdkGenerator):
    name = "cpp"

    def generate(self, context: GenerationContext, entries: list[SchemaEntry]) -> list:
        artifacts = generate_flatbuffers_cpp(
            entries,
            context.sdk_name,
            context.schema_dir,
            context.schema_paths,
            context.cpp_root,
            context.flatc_bin,
        )

        header_path = context.cpp_root / f"{context.effective_public_name}.h"
        header_path.parent.mkdir(parents=True, exist_ok=True)
        header_path.write_text(
            generate_header(
                entries,
                context.sdk_name,
                context.sdk_version,
                context.flatc_version,
                schema_set_sha256(context),
                context.effective_public_name,
            ),
            encoding="utf-8",
        )

        bridge_artifacts = []
        if context.cpp_python_bridge:
            bridge_artifacts = generate_python_bridge(context, entries)

        return [header_path, *artifacts, *bridge_artifacts]
