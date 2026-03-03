################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

import logging
import sys


def setup_expkits_logger(verbose=False, log_output="stdout", log_file="expkits_ci.log"):
    """Set up the logger based on the provided configuration."""
    logger = logging.getLogger("expkits_ci")
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
    return logging.getLogger("expkits_ci")
