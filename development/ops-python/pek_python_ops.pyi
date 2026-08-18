from typing import Final

import numpy


class Tensor:
    index: Final[int]
    name: Final[str | None]
    array: Final[numpy.ndarray]
    scale: Final[float]
    zero_point: Final[float]
    quantized: Final[bool]
