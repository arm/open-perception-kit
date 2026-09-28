################################################################
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
################################################################

from __future__ import annotations

from abc import ABC, abstractmethod
from pathlib import Path

from ..types import GenerationContext, SchemaEntry


class CppIntegrationGenerator(ABC):
    """Interface for C++ build-system integration generators such as CMake and Meson."""

    name: str

    @abstractmethod
    def write_outputs(self, context: GenerationContext, entries: list[SchemaEntry]) -> list[Path]:
        raise NotImplementedError
