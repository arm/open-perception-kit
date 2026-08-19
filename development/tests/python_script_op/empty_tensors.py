################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from perception.guest import Envelope
from pek_python_ops import Tensor


def process(env: Envelope, tensors: tuple[Tensor, ...]) -> None:
    assert env is not None
    assert tensors == ()
    assert producer_info.instanceId.endswith("/PythonScript-0")
    assert producer_info.component == "pek-python-ops/PythonScript"
    assert producer_info.implementation == "empty_tensors.py"
