---
sidebar_position: 5
sidebar_label: Inference Engines
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


# Inference Engines

Inference backends are isolated behind backend-specific Ops and shared libraries.
The current goal is to keep model loading and forward execution backend-specific
while preserving generic preprocessing, postprocessing, and `FrameResults` output.

## Status Summary

| Engine | Status | Notes |
| --- | --- | --- |
| ONNX Runtime | Working | Main cross-platform baseline and broad model compatibility path. |
| ExecuTorch | Experimental | In-tree evaluation path for edge-focused PyTorch deployment. |
| MNN | Planned | Candidate backend for mobile/embedded GPU acceleration. |

## Backend Notes

ONNX Runtime is the reference software backend because it is mature, widely used,
and useful for validating pipelines across host platforms.

Each ONNX Inference Op owns a session and accepts the optional
`intraOpNumThreads` and `interOpNumThreads` attributes in its OpChain. The backend
validates both counts before creating the session. For intra-op threading, zero
delegates selection to ORT; a positive integer sets the pool size. Omission keeps
the existing hardware-thread-count minus one policy, with a minimum/fallback of
one. Inter-op omission or one selects sequential graph execution. Zero selects parallel execution with an ORT-managed inter-op pool;
larger counts select parallel execution with that pool size. Both settings take
effect when the OpChain is created; changing `active` does not recreate or retune the session.

See [ONNX thread tuning](../public/how-to/performance-measurement.md)
for configuration and deployment guidance.

ExecuTorch is present for evaluation and should be treated as experimental until
its model support, tests, and integration behavior are made stable.

MNN is a planned backend, not a supported runtime path today.

For current architectural constraints, see [Known Limitations](known-limitations.md).
