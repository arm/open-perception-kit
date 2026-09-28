# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

from typing import Final, Protocol

import numpy
from open_perception_kit.fb.open_perception_kit.metadata.ProducerInfo import ProducerInfoT
from open_perception_kit.guest import Envelope


class Context:
    """Call-scoped metadata for one Python operation invocation."""

    producer_info: Final[ProducerInfoT]


class Tensor:
    """Call-scoped tensor metadata with a zero-copy, read-only NumPy view."""

    index: Final[int]
    name: Final[str | None]
    array: Final[numpy.ndarray]
    scale: Final[float]
    zero_point: Final[float]
    quantized: Final[bool]


class ProcessCallback(Protocol):
    """Callable contract for the module-level PythonScript entry point."""

    def __call__(
        self,
        env: Envelope,
        tensors: tuple[Tensor, ...],
        context: Context,
        /,
    ) -> None: ...


def python_script(callback: ProcessCallback, /) -> ProcessCallback:
    """Type-check and return a PythonScript entry point unchanged."""
    ...
