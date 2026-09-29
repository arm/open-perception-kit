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

# opchain-exec

`opchain-exec` is a proof-of-concept command-line application for running one OPK
OpChain on one image through the public C++ runtime API, then consuming the
returned packet with the generated Open Perception Kit C++ SDK.

The current version loads a PNG/JPEG image file with `opk::runtime::Tools`, wraps
the decoded BGRA pixels as a `opk::runtime::VideoFrame`, executes an OpChain JSON
file through `opk::runtime::OpChain`, validates the binary FrameResults packet
through the example-local `PerceptionPacket` helper, and demonstrates typed
generated Open Perception Kit payload handling with lambdas. The source intentionally
avoids direct `op/`, `mediaio/`, and internal `opk/Perception` headers, but it
does include generated `open_perception_kit::metadata::*` payload types because typed
result consumption is part of the example.

## Build

Build the main OPK development tree:

```sh
./scripts/build.sh debug true
```

The example is part of the main Meson build and is staged to
`tools/opchain-exec` beside `tools/opk-menu`.

## Run

```sh
./tools/opchain-exec \
  config/models/ultraface-rfb-320/opchain.json \
  data/images/my-image.jpg
```

The first argument is the OpChain JSON file. The second argument is the input
image file. Supported image extensions are `.png`, `.jpg`, and `.jpeg`.

`opchain-exec` prints the FrameResults packet size and a terminal-friendly dump of
known FrameResults payloads after the OpChain finishes. Each typed branch uses
the example-local `TextDisplay` helper to print payload content, including
detection labels, confidence scores, object ids, and bounding boxes when present.
Unknown payload types are reported as `Unknown payload type`.

Runtime components may also print diagnostics while the chain is being built or
executed; this example intentionally keeps that behavior visible so the source
stays focused on runtime execution plus typed Open Perception Kit result consumption.
