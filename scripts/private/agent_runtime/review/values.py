#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations


def required_float(value: object, field_name: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float, str)):
        raise ValueError(f"Review field '{field_name}' must be numeric.")
    return float(value)


def required_int(value: object, field_name: str) -> int:
    if isinstance(value, bool) or not isinstance(value, (int, str)):
        raise ValueError(f"Review field '{field_name}' must be an integer.")
    return int(value)
