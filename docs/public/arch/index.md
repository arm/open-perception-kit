---
slug: /arch
sidebar_position: 1
sidebar_label: Overview
---

# Architecture overview

This section documents the current AMP Development Forge runtime layout,
execution model, and main GStreamer elements.

# 📚 Documentation Map

## General overview

- [Project overview](project-overview.md)
  High-level introduction, purpose, scope, and goals.

- [Architecture](architectural-overview.md)
  Architectural overview of the system.

- [Containers](containers.md)
  Container runtime and development environment notes.

- [Inference engines](inference-engines.md)
  Runtime backend overview.

- [amp::Model](model.md)
  The amp::Model object.

- [Inference process](engine-independent.md)
  The inference engine independent inference process.

- [Types](types.md)
  Generic types.

## Execution engine

- [Perception](perception.md)
  Perception is the persistent metadata container.

- [Op system](op-system.md)
  Local processing based on micro-pipelines.

- [OpChain Context](op-chain-context.md)
  Transient runtime data model and ownership rules.

- [OpChain Example](op-chain-example.md)
  The Op system in a simple example.

- [Tensor Builder](tensor-builder.md)
  The input tensors are built by the tensor builders.

- [Tensor Parser](tensor-parser.md)
  The output tensors are parsed by one of the tensor parsers.

## Elements

- [ampinfer](elements/ampinfer.md)
  Details of the inference element.

- [amposd](elements/amposd.md)
  Details of the drawing element.

- [ampperformance](elements/ampperformance.md)
  Details about the performance measurement system.

- [ampsink](elements/ampsink.md)
  Details about the WebRTC presentation system.

## Supporting topics

- [Streamline and Performix setup](streamline-and-performix-setup.md)
  Notes for external profiling tools.
