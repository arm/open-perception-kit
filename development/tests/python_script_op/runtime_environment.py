################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import os
import sys
from pathlib import Path

import flatbuffers
import numpy

from perception.guest import Envelope
from pek_python_ops import Context, Tensor


def process(env: Envelope, tensors: tuple[Tensor, ...], context: Context) -> None:
    assert env is not None
    assert tensors == ()
    assert context.producer_info.implementation == "runtime_environment.py"
    assert flatbuffers.__version__
    assert numpy.__version__
    assert Path(sys.executable).is_file()

    runtime_venv = os.environ.get("PEK_PYTHON_RUNTIME_VENV") or os.environ.get(
        "PEK_DEVTOOLS_VENV"
    )
    if runtime_venv:
        expected_executable = Path(runtime_venv, "bin", "python").resolve()
        assert Path(sys.executable).resolve() == expected_executable
