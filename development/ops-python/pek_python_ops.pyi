################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from typing import Final

import numpy
from perception.fb.perception.metadata.ProducerInfo import ProducerInfoT


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
