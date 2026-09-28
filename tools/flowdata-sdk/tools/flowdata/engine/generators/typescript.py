################################################################
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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
