# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

import logging
import os
import sys


def setup_opk_logger(verbose=False, log_output="stdout", log_file="opk_ci.log"):
    """Set up the logger based on the provided configuration."""
    if log_output in ("file", "both") and os.path.exists(log_file):
        raise FileExistsError(
            f"Log file already exists: {log_file}. "
            "Remove it or choose a different --log-file path.")

    logger = logging.getLogger("opk_ci")
    logger.handlers = []
    logger.propagate = False
    level = logging.DEBUG if verbose else logging.ERROR
    fmt = "[%(levelname)s] %(message)s"

    formatter = logging.Formatter(fmt)
    logger.setLevel(level)

    if log_output == "stdout":
        stream_handler = logging.StreamHandler(sys.stdout)
        stream_handler.setFormatter(formatter)
        logger.addHandler(stream_handler)
    elif log_output == "file":
        file_handler = logging.FileHandler(log_file)
        file_handler.setFormatter(formatter)
        logger.addHandler(file_handler)
    elif log_output == "both":
        stream_handler = logging.StreamHandler(sys.stdout)
        stream_handler.setFormatter(formatter)
        file_handler = logging.FileHandler(log_file)
        file_handler.setFormatter(formatter)
        logger.addHandler(stream_handler)
        logger.addHandler(file_handler)

    return logger


def get_logger():
    """Get the configured logger."""
    return logging.getLogger("opk_ci")
