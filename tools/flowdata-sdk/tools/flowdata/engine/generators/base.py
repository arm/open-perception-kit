################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from abc import ABC, abstractmethod
from pathlib import Path

from ..types import GenerationContext, SchemaEntry


class SdkGenerator(ABC):
    name: str

    @abstractmethod
    def generate(self, context: GenerationContext, entries: list[SchemaEntry]) -> list[Path]:
        raise NotImplementedError
