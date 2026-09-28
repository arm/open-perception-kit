# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

from __future__ import annotations

from ..python.python_sdk_codegen import generate_python_sdk
from ..types import GenerationContext, SchemaEntry
from .base import SdkGenerator


class PythonSdkGenerator(SdkGenerator):
    name = "python"

    def generate(self, context: GenerationContext, entries: list[SchemaEntry]):
        return generate_python_sdk(entries, context)
