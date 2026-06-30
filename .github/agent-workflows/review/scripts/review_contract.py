#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from enum import Enum


MARKER = "<!-- agent-review-comment -->"
STATE_MARKER = "<!-- agent-review-state "
INLINE_MARKER = "<!-- agent-review-inline -->"
INLINE_STATE_MARKER = "<!-- agent-review-inline-state "
DEFAULT_AUTHOR_LOGINS = frozenset({"github-actions", "github-actions[bot]"})
GITHUB_API_VERSION = "2022-11-28"
GITHUB_USER_AGENT = "amp-dev-forge-agent-review"


class ReviewRecommendation(str, Enum):
    APPROVE = "approve"
    COMMENT = "comment"
    REQUEST_CHANGES = "request_changes"


class ReviewSeverity(str, Enum):
    NOTE = "note"
    MAJOR = "major"
    CRITICAL = "critical"


class DiffSide(str, Enum):
    LEFT = "LEFT"
    RIGHT = "RIGHT"


SEVERITY_COLORS = {
    ReviewSeverity.NOTE.value: "1f6feb",
    ReviewSeverity.MAJOR.value: "d97706",
    ReviewSeverity.CRITICAL.value: "dc2626",
}
RECOMMENDATION_COLORS = {
    ReviewRecommendation.APPROVE.value: "15803d",
    ReviewRecommendation.COMMENT.value: "2563eb",
    ReviewRecommendation.REQUEST_CHANGES.value: "dc2626",
}
GITHUB_REVIEW_EVENTS = {
    ReviewRecommendation.APPROVE.value: "APPROVE",
    # Non-blocking Agent comments should land as accepted-with-comments in PR UI.
    ReviewRecommendation.COMMENT.value: "APPROVE",
    ReviewRecommendation.REQUEST_CHANGES.value: "REQUEST_CHANGES",
}
