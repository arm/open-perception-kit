################################################################
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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
