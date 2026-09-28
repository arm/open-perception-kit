# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

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
