################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from open_perception_kit.guest import Envelope
from opk_python_ops import Context, Tensor


def process(env: Envelope, tensors: tuple[Tensor, ...], context: Context) -> None:
    raise RuntimeError("intentional Python failure")
