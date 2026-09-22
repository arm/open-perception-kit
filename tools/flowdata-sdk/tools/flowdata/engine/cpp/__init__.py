################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

from .base import CppIntegrationGenerator
from .cmake import CMakeIntegrationGenerator
from .meson import MesonIntegrationGenerator

_CPP_INTEGRATION_GENERATORS: tuple[CppIntegrationGenerator, ...] = (
    CMakeIntegrationGenerator(),
    MesonIntegrationGenerator(),
)


def supported_integration_names() -> list[str]:
    return [generator.name for generator in _CPP_INTEGRATION_GENERATORS]


def get_integration_generators(names: list[str]) -> list[CppIntegrationGenerator]:
    requested = set(names)
    return [generator for generator in _CPP_INTEGRATION_GENERATORS if generator.name in requested]
