################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import sys

from perception.guest import Envelope
from opk_python_ops import Context, Tensor


MARKER = "added-during-process"


def process(env: Envelope, tensors: tuple[Tensor, ...], context: Context) -> None:
    assert env is not None
    assert tensors == ()
    assert context.producer_info.implementation == "mutate_sys_path_repeatedly.py"
    assert MARKER not in sys.path
    sys.path.append(MARKER)
