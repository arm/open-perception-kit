#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum


DEFAULT_OPENAI_BASE_URL = "https://openai-api-proxy.geo.arm.com/api/providers/openai-eu/v1"
DEFAULT_AGENT_MODEL_CONFIG_PATH = ".github/agent-workflows/runtime/agent-models.json"
OPENAI_API_KEY_ENV = "OPENAI_API_KEY"  # pragma: allowlist secret
OPENAI_PROXY_KEY_ENV = "OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS"
OPENAI_BASE_URL_ENV = "OPENAI_BASE_URL"
OPENAI_AGENTS_DISABLE_TRACING_ENV = "OPENAI_AGENTS_DISABLE_TRACING"
OPENAI_AGENTS_DISABLE_TRACING_VALUE = "1"


class AgentCommand(str, Enum):
    REVIEW = "run-review"
    REPAIR = "run-repair"
    STABILIZATION = "run-stabilization"


class AgentInstance(str, Enum):
    REVIEW = "review"
    REPAIR = "repair"
    STABILIZATION = "stabilization"


AGENT_COMMAND_DEFAULT_INSTANCES = {
    AgentCommand.REVIEW: AgentInstance.REVIEW,
    AgentCommand.REPAIR: AgentInstance.REPAIR,
    AgentCommand.STABILIZATION: AgentInstance.STABILIZATION,
}


@dataclass(frozen=True)
class AgentTaskLimits:
    """Execution budget and preflight size limits for one agent task type."""

    max_turns: int
    max_prompt_chars: int
    max_review_files: int | None = None
    max_review_changed_lines: int | None = None


AGENT_TASK_LIMITS = {
    AgentCommand.REVIEW: AgentTaskLimits(
        max_turns=40,
        max_prompt_chars=180_000,
        max_review_files=120,
        max_review_changed_lines=10_000,
    ),
    AgentCommand.REPAIR: AgentTaskLimits(
        max_turns=30,
        max_prompt_chars=140_000,
    ),
    AgentCommand.STABILIZATION: AgentTaskLimits(
        max_turns=30,
        max_prompt_chars=120_000,
    ),
}


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


@dataclass(frozen=True)
class UnsupportedReviewClaimGuard:
    """Rule for dropping Agent review findings contradicted by current checkout evidence."""

    name: str
    evidence_tokens: tuple[str, ...]
    claim_markers: tuple[str, ...] = ()
    require_token_absent_from_anchor: bool = False

    def matches(self, *, finding_text: str, anchor_text: str) -> bool:
        normalized_finding = finding_text.lower()
        normalized_anchor = anchor_text.lower()
        if not any(token.lower() in normalized_finding for token in self.evidence_tokens):
            return False
        if self.claim_markers and not any(marker.lower() in normalized_finding for marker in self.claim_markers):
            return False
        if self.require_token_absent_from_anchor:
            return any(
                token.lower() in normalized_finding and token.lower() not in normalized_anchor
                for token in self.evidence_tokens
            )
        return True


UNSUPPORTED_REVIEW_CLAIM_GUARDS = (
    UnsupportedReviewClaimGuard(
        name="removed legacy review contract",
        evidence_tokens=(
            "codex-review",
            "codex-stabilize-pr.yml",
            "codex_model",
            "CODEX_REVIEW",
            "openai/codex-action@v1",
            "codex exec",
        ),
        require_token_absent_from_anchor=True,
    ),
    UnsupportedReviewClaimGuard(
        name="verified available action ref",
        evidence_tokens=(
            "actions/checkout@v6",
            "actions/upload-artifact@v6",
        ),
        claim_markers=(
            "currently published major version",
            "non-existent",
            "unable to resolve action",
        ),
    ),
)
