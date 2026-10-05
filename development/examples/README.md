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

# Development Examples

This directory contains proof-of-concept applications that are tightly coupled
to the checked-in Open Perception Kit runtime, configuration, pipeline, and SDK
surfaces.

The C++ runtime API examples are part of the main Meson build. Running
`./scripts/build.sh` builds them with the repository's normal dependencies and
stages their binaries into `tools/` beside `opk-menu`.

## Available Examples

- `opchain-exec`: command-line image input proof of concept that loads an image
  through `opk::runtime::Tools`, wraps it as a `opk::runtime::VideoFrame`, runs
  an OpChain through `opk::runtime::OpChain`, and prints the serialized result.
- `pipeline-exec`: C++ application facade proof of concept for loading an OPK
  pipeline JSON through `opk::runtime::Pipeline` and receiving serialized
  FrameResults packet callbacks.
- `byom-blazeface`: Python/config application sketch that brings a BlazeFace
  model through OPK model, OpChain, pipeline, Python postprocessing, and
  FrameResults packet consumption surfaces.
