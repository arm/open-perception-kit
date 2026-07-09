#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from ..review.context import ReviewRunContext
from ..sdk_runtime import RunContextWrapper, function_tool


@function_tool
def get_review_context(wrapper: RunContextWrapper[ReviewRunContext]) -> dict[str, object]:
    """Return bounded review scope and explicitly untrusted pull request evidence."""

    return wrapper.context.model_payload()
