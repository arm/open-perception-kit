################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from perception.guest import Envelope
from pek_python_ops import Context, Tensor


def process(env: Envelope, tensors: tuple[Tensor, ...], context: Context) -> None:
    assert env is not None
    assert tensors == ()
    assert "producer_info" not in globals()
    assert context.producer_info.instanceId.endswith("/pek-python-ops-PythonScript-0")
    assert context.producer_info.component == "pek-python-ops/PythonScript"
    assert context.producer_info.implementation == "empty_tensors.py"
    try:
        context.producer_info = None
    except AttributeError:
        pass
    else:
        raise AssertionError("context properties must be read-only")
