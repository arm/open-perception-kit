################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

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
