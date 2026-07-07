#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations


STANDARD_AGENT_REVIEW_WORKFLOW: dict[str, str] = {
    "workflow_file": "agent-review.yml",
    "workflow_name": "Agent Review",
    "review_state_script": "scripts/private/agent_runtime/review/fetch.py",
}


def standard_agent_review_workflow() -> dict[str, str]:
    return dict(STANDARD_AGENT_REVIEW_WORKFLOW)
