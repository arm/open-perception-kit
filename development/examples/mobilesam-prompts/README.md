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

# MobileSAM streaming prompt example

Build this public GStreamer + generated SDK consumer in the development container:

```bash
cmake -S development/examples/mobilesam-prompts -B /tmp/mobilesam-prompts
cmake --build /tmp/mobilesam-prompts
/tmp/mobilesam-prompts/mobilesam-prompts config/models/mobile-sam-int8-executorch/opchain.json
```

Start from a built OPK environment with its GStreamer plugin and library paths
set, and provision the model through `scripts/download-models.py` first.
The camera is the default source. Pass a source pipeline as the second argument
for a repeating image, for example:

```bash
/tmp/mobilesam-prompts/mobilesam-prompts \
  config/models/mobile-sam-int8-executorch/opchain.json \
  'filesrc location=sample.jpg ! jpegdec ! imagefreeze is-live=true'
```

The finite demonstration submits an initial box, replaces it, cancels, starts a
new request and ends its session at ten-second intervals, then exits. Video
continues during absent-prompt periods. Results go to stdout and bus diagnostics
to stderr. Request IDs deliberately exceed signed 64-bit/JavaScript Number exact
ranges. Acknowledgement means accepted at ingress; results identify the revision
that actually executed.

See the [public prompt protocol](../../../docs/public/how-to/box-prompts.md).
This program never includes private OPK runtime headers and does not download
models. A real Cairn backend still needs the protocol bridge described there.
