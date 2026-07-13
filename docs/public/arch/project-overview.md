---
sidebar_position: 2
sidebar_label: Project Overview
---

# Project Overview

Perception Experience Kit is a framework for building and running AI-enabled
media workflows. It helps teams move from raw media or data input to structured
results through reusable, configurable processing stages.

## What It Provides

- GStreamer-based media pipeline integration.
- An Op-based execution model for preprocessing, inference, and postprocessing.
- Configurable workflows that can be run repeatedly across environments.
- Structured `Perception` results that downstream elements can render, track, or publish.
- Runtime information for development, debugging, and evaluation.

The inference capability is not tied to GStreamer itself. GStreamer is the
current integration layer for media transport and orchestration, while the
processing model is organized around reusable inference and postprocessing
stages.

## Scope and Limitations

The package is an execution framework, not a guarantee of model quality or
application suitability. Results depend on input quality, model quality, and
correct integration into the configured processing flow. Current architectural
constraints are tracked in [Known Limitations](known-limitations.md).
