---
sidebar_position: 7
sidebar_label: Inference Process
---
<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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


# Inference Execution Flow

OPK separates engine-specific model loading and inference execution from generic
preprocessing, tensor handling, postprocessing, and `FrameResults` output.
Backend-specific code lives in separate shared libraries so SDK dependencies stay
isolated from the core runtime.

![Engine Independent Architecture](../public/static/img/engine-independent.png)

## Flow

1. The selected backend loads the model file and extracts metadata such as tensor
   names, shapes, element types, and quantization parameters.
2. Backend metadata is converted into the engine-agnostic `opk::Model` runtime
   representation.
3. Generic preprocessing builds input tensors using model input metadata,
   `DataKind`, `Shape`, and quantization/normalization values.
4. The backend-specific inference Op calls the runtime and writes raw output
   tensors.
5. Generic postprocessing wraps those outputs in `TensorView`, parses them, and
   appends structured schema payloads to `FrameResults`.

```text
engine-specific load
  -> opk::Model
  -> generic preprocessing
  -> engine-specific inference execution
  -> generic postprocessing
```

## Design Principle

Inference engines are responsible for loading models and executing the forward
pass. Preprocessing, postprocessing, result formatting, and OpChain structure stay
generic. This allows backend swapping without rewriting parser logic or changing
the surrounding OpChain contract.
