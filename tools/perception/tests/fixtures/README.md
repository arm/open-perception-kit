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

# Open Perception Kit Rust Cross-Language Fixture

`write_packet.py` writes a deterministic packet using the newly generated Python
SDK during `./scripts/perception-sdk.sh generate`. The generated Rust tests read
`tests/fixtures/opk-box-detections.hex`, check the typed `BoxDetections` values
and the external payload `com.arm.opk.fixture`, then round-trip both entries.

This tests current Python-to-Rust interoperability rather than compatibility
with a historical SDK's content-derived IDs. The writer uses explicit field
assignments so it also works with the minimum supported FlatBuffers compiler.
Generation obtains the checksum-locked FlatBuffers wheel from the SDK descriptor
and runs the writer with site packages and environment-based imports disabled.
The wheel is used directly without installing anything into the host interpreter.

The fixture is OPK-owned test data. The generic FlowData generator does not
contain Open Perception Kit schemas, values, or product-specific compatibility tests.
