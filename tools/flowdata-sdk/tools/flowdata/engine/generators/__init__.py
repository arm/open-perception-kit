################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

from .base import SdkGenerator
from .cpp import CppSdkGenerator
from .typescript import TypeScriptSdkGenerator
from .python import PythonSdkGenerator
from .rust import RustSdkGenerator

_SDK_GENERATORS: tuple[SdkGenerator, ...] = (
    CppSdkGenerator(),
    TypeScriptSdkGenerator(),
    PythonSdkGenerator(),
    RustSdkGenerator(),
)


def supported_sdk_names() -> list[str]:
    return [g.name for g in _SDK_GENERATORS]


def get_sdk_generators(names: list[str]) -> list[SdkGenerator]:
    requested = set(names)
    return [g for g in _SDK_GENERATORS if g.name in requested]
