################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from ..typescript import generate_typescript_sdk
from ..types import GenerationContext, SchemaEntry
from .base import SdkGenerator


class TypeScriptSdkGenerator(SdkGenerator):
    name = "ts"

    def generate(self, context: GenerationContext, entries: list[SchemaEntry]):
        return generate_typescript_sdk(entries, context)


__all__ = ["TypeScriptSdkGenerator"]
