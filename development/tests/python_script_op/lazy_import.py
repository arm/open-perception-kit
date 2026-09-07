################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from perception.guest import Envelope
from pek_python_ops import Context, Tensor


def process(env: Envelope, tensors: tuple[Tensor, ...], context: Context) -> None:
    from lazy_dependency import VALUE

    assert env is not None
    assert tensors == ()
    assert context.producer_info.implementation == "lazy_import.py"
    assert VALUE == "loaded during process"
