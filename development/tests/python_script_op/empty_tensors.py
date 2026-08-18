################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from perception.guest import Envelope
from pek_python_ops import Tensor


def process(env: Envelope, tensors: tuple[Tensor, ...]) -> None:
    assert env is not None
    assert tensors == ()
