---
sidebar_label: Streaming box prompts
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

# Send streaming box prompts

Use `opkprompt` before an `opkinfer` branch containing a prompt-aware Op. A submitted
box continues until replaced, cancelled or its controlling session ends. The
target pixels arrive through the video pipeline; requests contain no image or
frame ID. The consuming Op chooses its behavior when no prompt is bound.

```text
camera / repeating image -> geometry conversion -> opkprompt -> opkinfer -> opkcomm
```

Configure `opkprompt inference-instance=mobileSam destination-element=mobileSam`.
`inference-instance` identifies the backend inference Op's `instanceId` inside
the chain. `destination-element` names its GStreamer `opkinfer` element and
prevents a different branch with a coincidentally identical Op ID from consuming
the prompt. `source-name` defaults to `pipelineVideoFrame`. Put crop, scale,
rotation and mirroring before `opkprompt`; the first implementation permits only `identity`/`queue` between ingress and
the named destination (at most 16 forwarding elements). Unknown, fan-out and
geometry-changing routes are rejected before attachment. Whole-buffer metadata
copies preserve the snapshot; region copies and geometry transforms do not.
A destination/geometry mismatch fails explicitly. Use one ingress per selected
branch; do not tee prompted buffers into unrelated inference destinations.

The chain must advertise matching format, inference-instance and source support
through `OpInterfacePromptConsumer`. Unsupported destinations fail before inference.
Native Op implementations prepare prompt tensors and associate their own results;
the transport does not infer support from a model name.

## Public GStreamer protocol version 1

Locate the named `opkprompt` with `gst_bin_get_by_name()` and call
`gst_element_send_event()` on that element. It consumes each command event;
the caller must not unref it after sending. Use `GST_EVENT_CUSTOM_DOWNSTREAM_OOB`
with a `GstStructure` named `opk-prompt-command-v1`:

| Field | Type | Meaning |
| --- | --- | --- |
| `command` | `G_TYPE_STRING` | `submit`, `cancel` or `end-session`. |
| `session-id` | `G_TYPE_STRING` | Nonempty, at most 128 bytes; unique for the lifetime of the ingress element. |
| `payload` | `G_TYPE_BYTES` | For submit: FlatBuffers `BoxPromptInputs` with `BPRM` identifier, schema 1.0. |
| `request-id`, `revision` | `G_TYPE_UINT64` | For cancel: exact active request and revision; other commands use IDs in the generated payload. |

`submit` also replaces a continuing request when the same request ID has a
strictly greater revision. New request IDs start at revision 1. IDs are nonzero
uint64 and cannot be reused within a session, including after cancellation.
Changing destination requires a new request. Revisions are complete replacements.
Use generated SDK types and preserve all 64 bits (TypeScript uses `bigint`).
Build a `GBytes` copy, set it into the structure (which retains a reference),
then release the local `GBytes` reference before sending the event.

Box geometry is finite double-precision normalized continuous upright, unmirrored
image extent: `0 <= left < right <= 1`, `0 <= top < bottom <= 1`. Undo display
letterboxing/mirroring before submitting. The region stays fixed; it does not
track an object. A present payload with missing/invalid geometry is rejected.
An omitted payload is valid and means no prompting for that accepted revision;
no prompt meta or invented envelope reaches the OpChain. Cancel/session-end are
explicit state commands; missing per-buffer metadata never cancels a request.

Validation is synchronous and never waits for inference. Invalid updates retain
the previous state. At most 4096 serialized bytes, 4096 distinct IDs per session,
8 retained revisions and 1024 ended-session tombstones are accepted by an ingress
instance. These bounds include snapshots held by downstream queues. Over-budget
updates are rejected without evicting paired work. Release old buffers before
retrying an over-budget update; recreate the ingress after its session-history
limit. No ingress preparation queue or automatic retry is implemented.

## Acknowledgements, results and lifecycle

Ingress posts a `GST_MESSAGE_ELEMENT` structure `opk-prompt-ack-v1`:
`boundary="opkprompt"`, `command`, `session-id`, `request-id`/`revision` (uint64),
`accepted` (boolean) and `reason` (string). Invalid binary packets may have zero
IDs because they cannot be decoded. Event success and acknowledgement only
establish ingress acceptance. There is no first-use/applied notification.
`opkinfer` reports prompt-related invocation failures separately through
`opk-prompt-failure-v1` (`boundary`, session/request/revision, `reason`) and normal
GStreamer error messages.

Each outgoing video buffer carries its own immutable private `PromptInputsMeta`.
Replacement, cancellation and session end affect later attachments; already
paired buffers retain their original snapshot and may still execute. `opkinfer`
consumes its input-only metadata even when inactive or skipping inference.
Prompt-aware producers append typed results and `PromptAssociation` through
`FrameResultsMeta`/`opkcomm`. The association
contains the generated request envelope, owning `session_id`, media `parent_id`
and producing layer. Default/unprompted results claim no old prompt identity.
Consumers must route by session as well as request/revision to handle late results.

Only one session controls the destination. Cancellation retains that ownership;
end-session releases it, clears future attachment state and rejects later commands
from the ended session. Old buffers continue owning their data until released.
Source interruption/flush retains the continuing prompt while the controller is
alive; geometry and chain compatibility are checked on resume. Runtime recreation
requires destination revalidation; snapshots never retain backend tensor pointers.

## Cairn integration status

The application/backend uses public GStreamer events, GBytes and bus messages,
plus generated SDK packets. It must not depend on private C++ metadata layouts.
A remote client needs a backend bridge in the process that owns the pipeline.

The inspected [Cairn backend at revision 700d46e](https://github.com/arm/composable-ai-runtimes/blob/700d46ea0794a5ffc93352dafc9446d9cca9e061/services/perception/src/perception_service/gstreamer/runtime.py)
only exposes pipeline/stage control and handles error/EOS/state bus messages.
It needs command submission and element-bus acknowledgement forwarding before
this path can pass the actual CairnSDK acceptance gate. Public GStreamer
interfaces alone do not establish end-to-end Cairn support.

See the [GStreamer event ownership contract](https://gstreamer.freedesktop.org/documentation/gstreamer/gstelement.html#gst_element_send_event),
[metadata transform contract](https://gstreamer.freedesktop.org/documentation/gstreamer/gstmeta.html)
and [buffer writability contract](https://gstreamer.freedesktop.org/documentation/gstreamer/gstbuffer.html).
