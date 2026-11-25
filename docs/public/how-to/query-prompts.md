<!-- SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Text and visual category prompts

The native prompt path supports text category groups (`TPRM`) and owned RGB
reference groups (`VPRM`) alongside the existing MobileSAM box protocol (`BPRM`).
Set `opkprompt formats=TPRM` or `formats=VPRM` to enable the required ingress
format. The downstream operation must independently accept that format and the
explicit inference/source destination. Existing box-only presets retain BPRM.

Submit generated FlatBuffers packets in the existing `opk-prompt-command-v1`
`submit` event with `session-id` and a `GBytes` payload. The ingress owns the decoded
bytes before acknowledging success. Caller storage can then be released. Cancel
uses the active request ID and exact revision; `end-session` removes future
pairing. Already-paired buffers keep their immutable old request identity.

Text groups contain unique caller-owned group IDs and UTF-8 category descriptions.
Visual groups reference an owned table of complete upright, unmirrored RGB8 images
and normalized boxes in those references. They do not refer to target-frame IDs,
URLs or retained preview frames. Invalid packets leave the previous active request
unchanged. The semantic bounds are declared in `QueryPromptInputs.h` and advertised
by `opkprompt capabilities`; BPRM retains its 4096-byte limit.

Results use `PromptAssociation.matches` to connect produced object IDs to the
original group IDs. Multiple objects may match one group. A valid zero-detection
result has an empty match list; missing or failed prompts produce no query
association. Do not interpret model query indices as global class IDs.

For the real native example, locally materialized model files, exact numerical
contracts and qualification boundaries, see the
[YOLO-E integration instructions](../../../config/models/yoloe-v8s-onnx/README.md).
The example exercises actual ingress, learned preparation, inference, group
mapping, cancel and session end. Browser transport and UI are separate work.
