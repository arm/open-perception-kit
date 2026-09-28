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

# SDK Generator Sources

This directory contains ordinary tracked source files for the SDK generator,
including C++, Python, Rust, TypeScript, CMake, Meson, and the embedded Python
bridge. The generator identity is 0.6.1, independent of the generated Open Perception Kit
SDK's product version.

No separate checkout, credentials, or generator download is required. Source
updates are manual repository changes; there is no fetch or update command.
Generation receipts record the local Python source files and their hashes so
changes require regeneration just like other generation inputs.

From the repository root, use the supported container workflow:

```bash
./scripts/perception-sdk.sh generate
./scripts/perception-sdk.sh check
python3 tools/perception/tests/test_release.py
```

The descriptor at `tools/perception/sdk.json` selects this directory, the
generator entrypoint, the Open Perception Kit schema set, and output locations. Do not
edit generated SDK files or receipts by hand.

- [Open Perception Kit SDK workflow](../../docs/public/how-to/use-perception-sdk.md)
- [Generator commands and contracts](tools/flowdata/README.md)
- [Language API guides](docs/README.md)
- [Container model](HETEROGENEOUS_CONTAINER.md)
- [Generator limitations](KNOWN_LIMITATIONS.md)

The language guides retain illustrative `metapoc` examples. Their demo schemas
and applications are not shipped here; OPK uses `schemas/perception/metadata/`.
Repository contribution and source-header rules apply to these sources.
