#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import os
from typing import TypeVar

from .contracts import (
    DEFAULT_OPENAI_BASE_URL,
    OPENAI_AGENTS_DISABLE_TRACING_ENV,
    OPENAI_AGENTS_DISABLE_TRACING_VALUE,
    OPENAI_API_KEY_ENV,
    OPENAI_BASE_URL_ENV,
    OPENAI_PROXY_KEY_ENV,
)


def configure_openai_defaults() -> None:
    os.environ.setdefault(OPENAI_BASE_URL_ENV, DEFAULT_OPENAI_BASE_URL)
    os.environ.setdefault(OPENAI_AGENTS_DISABLE_TRACING_ENV, OPENAI_AGENTS_DISABLE_TRACING_VALUE)
    if not os.environ.get(OPENAI_API_KEY_ENV) and os.environ.get(OPENAI_PROXY_KEY_ENV):
        os.environ[OPENAI_API_KEY_ENV] = os.environ[OPENAI_PROXY_KEY_ENV]


def configure_openai_environment() -> None:
    configure_openai_defaults()
    if not os.environ.get(OPENAI_API_KEY_ENV):
        raise RuntimeError(
            f"{OPENAI_API_KEY_ENV} or {OPENAI_PROXY_KEY_ENV} must be set for the OpenAI proxy."
        )


configure_openai_defaults()

# The Arm proxy can rely on corporate CAs from the system trust store. Keep
# truststore injection before importing the OpenAI Agents SDK or its httpx stack.
# autopep8: off
import truststore
truststore.inject_into_ssl()

from agents import Agent, RunConfig, Runner, function_tool  # noqa: E402
from pydantic import BaseModel, ConfigDict, Field  # noqa: E402
# autopep8: on


ModelOutput = TypeVar("ModelOutput", bound=BaseModel)


def coerce_model_output(model_type: type[ModelOutput], output: object) -> ModelOutput:
    if isinstance(output, model_type):
        return output
    if isinstance(output, str):
        return model_type.model_validate_json(output)
    return model_type.model_validate(output)
