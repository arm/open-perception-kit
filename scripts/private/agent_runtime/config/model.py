#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import sys

if __package__ in (None, ""):  # pragma: no cover - used for direct script execution.
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    __package__ = "agent_runtime.config"

from ..contracts import (
    AgentInstance,
    DEFAULT_AGENT_MODEL_CONFIG_PATH,
    load_json_object,
    parse_enum_value,
    require_non_empty_string,
    require_object,
)


@dataclass(frozen=True)
class AgentModelEntry:
    model: str


@dataclass(frozen=True)
class AgentModelConfig:
    default_agent_model: str
    agents: dict[AgentInstance, AgentModelEntry]


def load_agent_model_config(config_file: str | Path) -> AgentModelConfig:
    payload = load_json_object(config_file, "Agent model config")
    default_agent_model = require_non_empty_string(
        payload.get("default_agent_model"),
        "default_agent_model",
    )
    agents_payload = require_object(payload.get("agents", {}), "agents")

    agents: dict[AgentInstance, AgentModelEntry] = {}
    for raw_instance, raw_entry in agents_payload.items():
        instance = parse_enum_value(AgentInstance, raw_instance, "agent instance")
        entry = require_object(raw_entry, f"agents.{raw_instance}")
        agents[instance] = AgentModelEntry(
            model=require_non_empty_string(entry.get("model"), f"agents.{raw_instance}.model"),
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

    instance = parse_enum_value(AgentInstance, agent_instance, "agent instance")
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
