################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from ..rust import generate_rust_sdk
from ..types import GenerationContext, SchemaEntry
from .base import SdkGenerator


class RustSdkGenerator(SdkGenerator):
    name = "rust"

    def generate(self, context: GenerationContext, entries: list[SchemaEntry]):
        return generate_rust_sdk(entries, context)


__all__ = ["RustSdkGenerator"]
