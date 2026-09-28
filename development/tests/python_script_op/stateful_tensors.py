# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import numpy

from open_perception_kit.guest import Envelope
from opk_python_ops import Context, Tensor, python_script


call_count = 0


@python_script
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
