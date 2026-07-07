#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
import json
from pathlib import Path
from typing import Any, TypeVar


DEFAULT_OPENAI_BASE_URL = "https://openai-api-proxy.geo.arm.com/api/providers/openai-eu/v1"
DEFAULT_AGENT_MODEL_CONFIG_PATH = ".github/agent-runtime/runtime/agent-models.json"
DEFAULT_AGENT_TASK_CONFIG_PATH = ".github/agent-runtime/runtime/agent-tasks.json"
OPENAI_API_KEY_ENV = "OPENAI_API_KEY"  # pragma: allowlist secret
OPENAI_PROXY_KEY_ENV = "OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS"
OPENAI_BASE_URL_ENV = "OPENAI_BASE_URL"
OPENAI_AGENTS_DISABLE_TRACING_ENV = "OPENAI_AGENTS_DISABLE_TRACING"
OPENAI_AGENTS_DISABLE_TRACING_VALUE = "1"
MARKER = "<!-- agent-review-comment -->"
INLINE_MARKER = "<!-- agent-review-inline -->"

EnumValue = TypeVar("EnumValue", bound=Enum)


def load_json_value(config_file: str | Path) -> Any:
    path = Path(config_file)
    if not path.is_file():
        raise ValueError(f"JSON file does not exist: {path}")
    return json.loads(path.read_text(encoding="utf-8"))


def load_json_object(config_file: str | Path, config_name: str) -> dict[str, Any]:
    path = Path(config_file)
    payload = load_json_value(path)
    if not isinstance(payload, dict):
        raise ValueError(f"{config_name} must be a JSON object: {path}")
    return payload


def require_object(value: Any, field_name: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError(f"Config field '{field_name}' must be an object.")
    return value


def require_list(value: Any, field_name: str) -> list[Any]:
    if not isinstance(value, list):
        raise ValueError(f"Config field '{field_name}' must be a JSON array.")
    return value


def require_non_empty_string(value: Any, field_name: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"Config field '{field_name}' must be a non-empty string.")
    return value.strip()


def require_positive_int(value: Any, field_name: str) -> int:
    if not isinstance(value, int) or value <= 0:
        raise ValueError(f"Config field '{field_name}' must be a positive integer.")
    return value


def optional_positive_int(value: Any, field_name: str) -> int | None:
    if value is None:
        return None
    return require_positive_int(value, field_name)


def parse_enum_value(enum_type: type[EnumValue], value: str | EnumValue, field_name: str) -> EnumValue:
    if isinstance(value, enum_type):
        return value
    try:
        return enum_type(value)
    except ValueError as exc:
        allowed = ", ".join(str(item.value) for item in enum_type)
        raise ValueError(f"Unsupported {field_name} '{value}'. Expected one of: {allowed}.") from exc


class AgentCommand(str, Enum):
    REVIEW = "run-review"
    REPAIR = "run-repair"
    STABILIZATION = "run-stabilization"


class AgentInstance(str, Enum):
    REVIEW = "review"
    REPAIR = "repair"
    STABILIZATION = "stabilization"


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
    # <agent-review:suppress> Non-blocking Agent comments intentionally land as
    # accepted-with-comments in PR UI instead of leaving a pending review state.
    ReviewRecommendation.COMMENT.value: "APPROVE",
    ReviewRecommendation.REQUEST_CHANGES.value: "REQUEST_CHANGES",
}


@dataclass(frozen=True)
class UnsupportedReviewClaimGuard:
    """Rule for dropping Agent review findings contradicted by current checkout evidence."""

    name: str
    evidence_tokens: tuple[str, ...]
    claim_markers: tuple[str, ...] = ()
    require_token_absent_from_anchor: bool = False
    require_token_present_in_anchor: bool = False

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
        if self.require_token_present_in_anchor:
            return any(
                token.lower() in normalized_finding and token.lower() in normalized_anchor
                for token in self.evidence_tokens
            )
        return True


UNSUPPORTED_REVIEW_CLAIM_GUARDS = (
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
    UnsupportedReviewClaimGuard(
        name="verified agent runtime artifact context",
        evidence_tokens=(
            ".agent-runtime/source-run-repair/artifacts",
        ),
        claim_markers=(
            "outside the agent context",
            "downloaded outside",
        ),
        require_token_present_in_anchor=True,
    ),
)
