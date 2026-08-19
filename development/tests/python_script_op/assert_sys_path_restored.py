################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import sys

from perception.guest import Envelope
from pek_python_ops import Context, Tensor


def process(env: Envelope, tensors: tuple[Tensor, ...], context: Context) -> None:
    assert env is not None
    assert tensors == ()
    assert context.producer_info.implementation == "assert_sys_path_restored.py"
    assert "corrupted-by-script" not in sys.path
