################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import numpy

from perception.guest import Envelope
from pek_python_ops import Context, Tensor


call_count = 0


def process(env: Envelope, tensors: tuple[Tensor, ...], context: Context) -> None:
    global call_count
    call_count += 1

    assert env is not None
    assert context.producer_info.implementation == "stateful_tensors.py"
    assert "Context(producer_info=" in repr(context)
    assert len(tensors) == 2
    assert "Tensor(index=0" in repr(tensors[0])
    assert tensors[0].index == 0
    assert tensors[0].name == "scores"
    assert tensors[0].array.dtype == numpy.float32
    assert tensors[0].array.shape == (2, 2)
    assert tensors[0].array[0, 0] == call_count
    assert not tensors[0].array.flags.writeable
    assert not tensors[0].quantized

    assert tensors[1].index == 1
    assert tensors[1].name == "classes"
    assert tensors[1].array.dtype == numpy.int8
    assert tensors[1].array.tolist() == [1, 2]
    numpy.testing.assert_allclose(tensors[1].scale, 0.5)
    numpy.testing.assert_allclose(tensors[1].zero_point, 1.0)
    assert tensors[1].quantized

    try:
        tensors[0].array[0, 0] = 100
    except ValueError:
        pass
    else:
        raise AssertionError("tensor arrays must be read-only")

    try:
        tensors[0].array.setflags(write=True)
    except ValueError:
        pass
    else:
        raise AssertionError("tensor arrays must not become writeable")

    retained = tensors[0].array.copy()
    assert retained.flags.owndata
