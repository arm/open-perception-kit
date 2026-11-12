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

# Explicit result inputs

SDK payload types define data compatibility. Concrete producer IDs select outputs;
semantic labels, model names, parser names and categories do not activate dependencies
or select inputs. `contentType`, `content_type` and semantic capability roles are removed.

## Addresses and connections

An output reference contains `payloadType` and the exact `ProducerInfo.instance_id`.
Op producer IDs use the existing `<infer-id>/<instanceId>` identity. Assign explicit
operation instance IDs when another operation or pipeline configuration refers to them.

An Op exposes one address per payload type. Multiple batches of that type belong
to the same output; use separate Op instances when outputs need independent selection.

Each consumer declares locally named inputs with their SDK payload types. An `inputId`
is only an address of a socket on that consumer; producers do not advertise matching
input names. Compatibility never compares input names with producer labels.

`InferenceController` has one `input` socket for `BoxDetections`:

- No source attribute: process the full video frame.
- `sourceOp`: name one earlier operation in the same OpChain. Binding validates its
  payload type and rejects forward, missing or incompatible references.
- `sourceProducer`: select an exact external producer. An empty string explicitly
  declares an unconnected input; it never falls back to full-frame processing.

External connections can be overridden by `opkinfer`'s `input-bindings` JSON array:
`consumerId`, `inputId`, `payloadType`, and `producerId`. Changes are validated and
snapshotted for each frame. Invalid updates leave the previous connections intact.
Only the addressed, type-compatible upstream producer is automatically activated.
An unconnected or unavailable input does not borrow another publisher's results.

The model registry exposes concrete outputs, required inputs and outputs observed
upstream. The WebUI offers type-compatible upstream sources and changes connections
explicitly. Choosing a different model does not silently relabel its output.
The control message is `model_inputs` with `name` (the target element) and a
`bindings` array. Runtime changes survive browser reloads; save them in the
pipeline preset to retain them across pipeline restarts.

Python declarations use payload-only `providedOutputs` and named `requiredInputs`.
The read-only `context.input_producers` mapping supplies resolved input addresses
to callbacks; callbacks select data by those addresses, not by semantic strings.

Trackers select concrete input producers. Embedding-space identity remains a data
comparability requirement, alongside an explicit embedding producer. Rendering is
configured separately: circle/rectangle presentation can select producers without
changing the meaning or type of their data.
`opktracker input-producers` explicitly lists the box producers feeding one tracking
history. `use-embeddings=true` also requires `embedding-producer-id`. Native OSD
uses `circle-producers` and `box-producers`; browser overlays store the selected
renderer and source. Saved overlay v2 records migrate to v3 by retaining those
choices and removing retired selector fields. The previous record remains a backup.

## Migration and compatibility

Removing `LayerInfo.content_type` is a schema-set-wide breaking change. It remains
within the coordinated, unreleased SDK 1.0 migration; intermediate development
snapshots must be rebuilt together and are not mixed. Root IDs remain stable.

Model and OpChain configuration readers advance to major version 2 because removed
fields and input declaration contracts are incompatible. Checked-in descriptors and
examples migrate together. Pipeline presets retain their format major and increment
their patch when their element configuration changes. No legacy routing aliases or
semantic-role fallback are retained.

## Verification — EXPKITS-1569, 2026-11-12

This layer is based on adoption-verification commit `924b7fcf`, above PR #271.
Its checks cover the field removal and explicit connections, independently of
the earlier stack's verification record:

- Container build and Meson regression suite: **102 targets passed**.
- SDK source-snapshot check and SDK tooling suite: **135 tests passed**.
- Plumber FrameResults regressions: **47 tests passed**.
- Real Chrome overlay regressions: **8 tests passed**, including saved settings,
  mask pixels and OCR selection.
- Real RetinaFace/gaze models: all four detector-resolution/backbone combinations,
  blank input, and live switching/reactivation passed. The shared-pipeline test
  changes concrete input bindings alongside model activation.
- New regressions cover same-type producer isolation, unconnected inputs, stale
  crop clearing, forward/incompatible local references, duplicate addresses,
  invalid live updates, immutable Python connection snapshots, WebSocket
  control, UI ambiguity and v2-to-v3 overlay migration.
- `scripts/gen-doc.sh` completed, including Doxygen and PlantUML generation.
- Pre-commit checks passed; changed-line clang-tidy reported zero findings.
- Schema assessment against `origin/main` identifies major impact, covered by
  the coordinated `0.1.2` to unreleased `1.0.0` transition. The intermediate
  parent snapshot is not a released compatibility baseline.

The SDK remains the coordinated **unreleased 1.0.0** snapshot. No stable package
publication is part of this change. Full model execution used the ONNX CPU backend;
ExecuTorch/MLWL and physical-camera execution were not exercised by these checks.
