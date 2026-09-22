---
sidebar_position: 1
sidebar_label: Overview
---

# Architecture Documentation

This section documents the current OPK runtime layout,
execution model, metadata contracts, and main GStreamer elements.

## Start Here

- [Project overview](project-overview.md)
- [Architectural overview](architectural-overview.md)
- [Known limitations](known-limitations.md)

## Runtime Model

- [Inference engines](inference-engines.md)
- [Engine-independent inference flow](engine-independent.md)
- [opk::Model](model.md)
- [Op system](op-system.md)
- [OpChain Context](op-chain-context.md)
- [OpChain Example](op-chain-example.md)
- [FrameResults](perception.md)

## Tensor Contracts

- [Types](types.md)
- [Tensor Builder](tensor-builder.md)
- [Tensor Parser](tensor-parser.md)

## Elements

- [opkinfer](elements/opkinfer.md)
- [opkosd](elements/opkosd.md)
- [opkperformance](elements/opkperformance.md)
- [opksink](elements/opksink.md)

## Development And Deployment

- [Containers](containers.md)
- [Testing](testing.md)
- [Release packages](release-process.md)
