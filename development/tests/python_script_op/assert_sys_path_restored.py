################################################################
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
################################################################

import sys

from open_perception_kit.guest import Envelope
from opk_python_ops import Context, Tensor


def process(env: Envelope, tensors: tuple[Tensor, ...], context: Context) -> None:
    assert env is not None
    assert tensors == ()
    assert context.producer_info.implementation == "assert_sys_path_restored.py"
    assert "corrupted-by-script" not in sys.path
