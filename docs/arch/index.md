---
sidebar_position: 1
sidebar_label: Overview
---
<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
SPDX-License-Identifier: Apache-2.0

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->


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
