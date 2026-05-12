---
slug: /arch
sidebar_position: 1
sidebar_label: Overview
---

# Architecture Documentation

This section documents the current Perception Experience Kit runtime layout,
execution model, and main GStreamer elements.

## Documentation Map

## General overview

- [Project overview](project-overview.md)
  High-level introduction, purpose, scope, and goals.

- [Architecture overview](architectural-overview.md)
  Architectural overview of the system.

- [Containers](containers.md)
  Container runtime and development environment notes.

- [Inference engines](inference-engines.md)
  Runtime backend overview.

- [pek::Model](model.md)
  The pek::Model object.

- [Inference process](engine-independent.md)
  The inference engine independent inference process.

- [Types](types.md)
  Generic types.

- [Testing](testing.md)  
  Testing, validation and verification.

## Execution engine

- [Perception](perception.md)
  Perception is the persistent metadata container.

- [Op system](op-system.md)
  Local processing based on micropipelines.

- [OpChain Context](op-chain-context.md)
  Transient runtime data model and ownership rules.

- [OpChain Example](op-chain-example.md)
  The Op system in a simple example.

- [Tensor Builder](tensor-builder.md)
  The input tensors are built by the tensor builders.

- [Tensor Parser](tensor-parser.md)
  The output tensors are parsed by one of the tensor parsers.

## Elements

- [pekinfer](elements/pekinfer.md)
  Details of the inference element.

- [pekosd](elements/pekosd.md)
  Details of the drawing element.

- [pekperformance](elements/pekperformance.md)
  Details about the performance measurement system.

- [peksink](elements/peksink.md)
  Details about the WebRTC presentation system.
