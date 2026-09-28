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

import os
import sys
from pathlib import Path

import flatbuffers
import numpy

from open_perception_kit.guest import Envelope
from opk_python_ops import Context, Tensor


def process(env: Envelope, tensors: tuple[Tensor, ...], context: Context) -> None:
    assert sys.version_info[:2] == (3, 13)
    assert env is not None
    assert tensors == ()
    assert context.producer_info.implementation == "runtime_environment.py"
    assert flatbuffers.__version__
    assert numpy.__version__
    assert Path(sys.executable).is_file()

    runtime_venv = os.environ.get("OPK_PYTHON_RUNTIME_VENV") or os.environ.get(
        "OPK_DEVTOOLS_VENV"
    )
    if runtime_venv:
        expected_executable = Path(runtime_venv, "bin", "python").resolve()
        assert Path(sys.executable).resolve() == expected_executable
