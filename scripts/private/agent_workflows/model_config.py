#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

try:
    from .contracts import AgentInstance, DEFAULT_AGENT_MODEL_CONFIG_PATH
except ImportError:  # pragma: no cover - used when executed as a standalone script.
    from contracts import AgentInstance, DEFAULT_AGENT_MODEL_CONFIG_PATH


@dataclass(frozen=True)
class AgentModelEntry:
    model: str


@dataclass(frozen=True)
class AgentModelConfig:
    default_agent_model: str
    agents: dict[AgentInstance, AgentModelEntry]


def require_non_empty_string(value: Any, field_name: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"Agent model config field '{field_name}' must be a non-empty string.")
    return value.strip()


def parse_agent_instance(value: str | AgentInstance) -> AgentInstance:
    if isinstance(value, AgentInstance):
        return value
    try:
        return AgentInstance(value)
    except ValueError as exc:
        allowed = ", ".join(instance.value for instance in AgentInstance)
        raise ValueError(f"Unsupported agent instance '{value}'. Expected one of: {allowed}.") from exc


def load_agent_model_config(config_file: str | Path) -> AgentModelConfig:
    path = Path(config_file)
    if not path.is_file():
        raise ValueError(f"Agent model config file does not exist: {path}")

    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise ValueError(f"Agent model config must be a JSON object: {path}")

    default_agent_model = require_non_empty_string(
        payload.get("default_agent_model"),
        "default_agent_model",
    )
    agents_payload = payload.get("agents", {})
    if not isinstance(agents_payload, dict):
        raise ValueError("Agent model config field 'agents' must be an object.")

    agents: dict[AgentInstance, AgentModelEntry] = {}
    for raw_instance, raw_entry in agents_payload.items():
        instance = parse_agent_instance(raw_instance)
        if not isinstance(raw_entry, dict):
            raise ValueError(f"Agent model config entry '{raw_instance}' must be an object.")
        agents[instance] = AgentModelEntry(
            model=require_non_empty_string(raw_entry.get("model"), f"agents.{raw_instance}.model"),
        )

    return AgentModelConfig(default_agent_model=default_agent_model, agents=agents)


def resolve_agent_model(
    config_file: str | Path,
    agent_instance: str | AgentInstance,
    *,
    override_model: str = "",
) -> str:
    override = override_model.strip()
    if override:
        return override

    instance = parse_agent_instance(agent_instance)
    config = load_agent_model_config(config_file)
    entry = config.agents.get(instance)
    if entry is None:
        return config.default_agent_model
    return entry.model


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Resolve an Agent workflow model from runtime config.")
    parser.add_argument("agent_instance", choices=[instance.value for instance in AgentInstance])
    parser.add_argument("--config-file", default=DEFAULT_AGENT_MODEL_CONFIG_PATH)
    parser.add_argument("--override-model", default="")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    print(
        resolve_agent_model(
            args.config_file,
            args.agent_instance,
            override_model=args.override_model,
        ),
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
