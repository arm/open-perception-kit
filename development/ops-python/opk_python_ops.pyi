################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

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
