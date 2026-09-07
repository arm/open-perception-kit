################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from perception.guest import Envelope
from pek_python_ops import Context, Tensor


def process(env: Envelope, tensors: tuple[Tensor, ...], context: Context) -> None:
    raise RuntimeError("intentional Python failure")
