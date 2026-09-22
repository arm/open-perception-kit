################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from ..python.python_sdk_codegen import generate_python_sdk
from ..types import GenerationContext, SchemaEntry
from .base import SdkGenerator


class PythonSdkGenerator(SdkGenerator):
    name = "python"

    def generate(self, context: GenerationContext, entries: list[SchemaEntry]):
        return generate_python_sdk(entries, context)
