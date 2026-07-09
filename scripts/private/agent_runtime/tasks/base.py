#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
from abc import ABC, abstractmethod
from typing import Any

from ..runtime_context import AgentRunContext
from ..sdk_runtime import Agent, RunConfig, Runner


class AgentWorkflowTask(ABC):
    agent_name: str

    def add_cli_arguments(self, parser: argparse.ArgumentParser) -> None:
        del parser

    def validate_args(self, args: argparse.Namespace) -> None:
        del args

    def output_type(self) -> Any:
        """Return the optional structured SDK output type for this workflow task."""
        return None

    @abstractmethod
    def instructions(self) -> str:
        raise NotImplementedError("AgentWorkflowTask subclasses must define instructions.")

    @abstractmethod
    def tools(self) -> list[Any]:
        raise NotImplementedError("AgentWorkflowTask subclasses must define tools.")

    @abstractmethod
    def write_result(self, final_output: object, args: argparse.Namespace) -> int:
        raise NotImplementedError("AgentWorkflowTask subclasses must define result writing.")

    def build_agent(self, *, model: str) -> Agent[AgentRunContext]:
        agent_kwargs: dict[str, Any] = {
            "name": self.agent_name,
            "instructions": self.instructions(),
            "model": model,
            "tools": self.tools(),
        }
        output_type = self.output_type()
        if output_type is not None:
            agent_kwargs["output_type"] = output_type
        return Agent[AgentRunContext](**agent_kwargs)

    async def run_agent(
        self,
        input_text: str,
        *,
        model: str,
        max_turns: int,
        context: AgentRunContext,
    ) -> object:
        result = await Runner.run(
            self.build_agent(model=model),
            input_text,
            context=context,
            max_turns=max_turns,
            run_config=RunConfig(tracing_disabled=True),
        )
        return result.final_output
