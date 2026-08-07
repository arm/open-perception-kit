#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
from abc import ABC, abstractmethod
import time
from typing import Any

from ..diagnostics import log_agent_diagnostic
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

    def build_agent(self, *, model: str, context: AgentRunContext | None = None) -> Agent[AgentRunContext]:
        del context
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
        agent = self.build_agent(model=model, context=context)
        start = time.monotonic()
        log_agent_diagnostic(
            "agent_run_start",
            agent=self.agent_name,
            model=agent.model,
            max_turns=max_turns,
            context=type(context).__name__,
            input_chars=len(input_text),
        )
        try:
            result = await Runner.run(
                agent,
                input_text,
                context=context,
                max_turns=max_turns,
                run_config=RunConfig(tracing_disabled=True),
            )
        except Exception as exc:
            log_agent_diagnostic(
                "agent_run_failed",
                agent=self.agent_name,
                elapsed_ms=int((time.monotonic() - start) * 1000),
                error=type(exc).__name__,
            )
            raise
        final_output = result.final_output
        log_agent_diagnostic(
            "agent_run_done",
            agent=self.agent_name,
            elapsed_ms=int((time.monotonic() - start) * 1000),
            output=final_output,
            output_chars=len(str(final_output)),
        )
        return final_output
