from typing import Final

import numpy


class Tensor:
    """Call-scoped tensor metadata with a zero-copy, read-only NumPy view."""

    index: Final[int]
    name: Final[str | None]
    array: Final[numpy.ndarray]
    scale: Final[float]
    zero_point: Final[float]
    quantized: Final[bool]
