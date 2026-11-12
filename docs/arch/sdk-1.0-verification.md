---
sidebar_label: SDK 1.0 verification
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

# SDK 1.0 verification

**Status: overlay and RetinaFace/ResNet gaze integration verified on main `95199ff9`; review, current-head CI and release gates remain.**
This dated record supports [EXPKITS-1535](https://jira.arm.com/browse/EXPKITS-1535)
and [EXPKITS-1523](https://jira.arm.com/browse/EXPKITS-1523). It was refreshed on
2026-11-12 after the main rebase and review corrections. Verification does not authorize publication.

## Baselines and scope

The latest genuine numbered product release checked on 2026-11-10 is `v0.1.2`
(`95e61135df923d708bea7f8b161165055c92aa40`), corroborated by the GitHub Release,
changelog and product version. The implementation baseline before this refactor is
`d1e8c7d4b5a264e1b13d209a57b94e6b66f06dc7`. The approved target is SDK/product
`1.0.0`, an intentionally breaking coordinated migration.

The verified implementation snapshot is
`5fc05deae0e3da1a8ca75bb944d1c9803b57bc67`, [integration PR #270](https://github.com/arm/open-perception-kit/pull/270),
rebased onto main `95199ff9d6a864734a4ceb50fdb904d3a7d97ab6` in native stack #248.
That main includes semantic, instance and OCR overlays plus RetinaFace/ResNet gaze. Each owning PR records before/after behaviour,
regressions and intentional corrections. Valid workflows are preserved; bug fixes
recorded under [EXPKITS-1542](https://jira.arm.com/browse/EXPKITS-1542) are not presented
as unchanged behaviour. The earlier instruction to defer those findings was superseded.
The review fixes below preserve the existing SDK APIs, payload identities and
fixture bytes; generation receipts reflect the changed generator sources.

## Producer and consumer evidence

Paths below are repository-relative. The tests are runnable through the checked-in
Meson, SDK and WebUI workflows; optional model tests require their documented cache.

| Area | Producers and consumers reviewed | Representative evidence | Limit or remaining dependency |
| --- | --- | --- | --- |
| Identity and transport | Generated C++/Python/Rust/TypeScript SDKs, FrameResultsMeta, opkcomm, opksink, packet inspection | SDK identity/fixture tests, `gst_meta_wrapper_test.cpp`, `frame_results.test.mjs`, Plumber decoder tests | No old-packet adapter; opaque transport remains separate from semantic comparison |
| Image geometry and categories | Preprocessing, detector parsers, ROI controller, tracker, native/browser projections, Plumber | `image_geometry_test.cpp`, actual cached-model `image_geometry_pipeline_test.py`, category-zero and clipping regressions | Physical GPU execution is unverified |
| Classifications and domain outcomes | ImageNet, OCR recognition, contact/presence parsers, native/browser consumers, Plumber | `classification_results_test.cpp`, `classification_pipeline_test.py`, OCR wire/normalized-ROI tests and real-browser controls/label tests | OCR uses recognized-text kinds and explicit box subjects; no categorical-label or derivation-parent inference |
| Gaze | Parser, native/browser marker rendering, Plumber subject/wrap comparison | `gaze_detection_parser_test.cpp`, `gaze_pipeline_test.py`, browser and Plumber regressions | Angle calibration is preserved; no new model-accuracy claim |
| Masks | Matting, semantic and instance parsers; bitmap projection, native/browser overlays, Plumber | `mask_metadata_test.cpp`, `mask_pipeline_test.py`, parser/Plumber tests and real-browser pixel/control tests | Uint8/Uint32LE, declared void and image regions; bounded grids/classes/composition; absent instance IDs/scores/boxes stay absent |
| Embeddings | Parser, typed active/dormant tracker matching, inspection and Plumber | `embedding_contract_test.cpp`, tracker tests, live bridge and browser/Plumber tests | Controlled-vector correctness, not model-specific ReID accuracy |
| Tracking | Tracker/session identity, observation sources, prediction state, timed traces, renderers and Plumber | `tracking_contract_test.cpp`, two-tracker pipeline, pixel-label and chronological comparison regressions rerun after integration | No new model-accuracy or hardware claim |
| Performance | Numeric producer, CLI, WebUI and native formatting | `performance_metrics_test.cpp`, `performance_pipeline_test.py`, browser availability/format tests | Independent missing values remain unavailable, not zero |
| Audio | VAD/turn parsers, bound-producer gating, original-buffer context, conversation example, CLI/browser/Plumber | Audio parser/gating tests, `voice_conversation_test.cpp`, `audio_metadata_pipeline_test.py`, live bridge | Synthetic/model-free audio and deterministic gate tests; live microphone/device behaviour is unverified |
| Shared routing and availability | Op interfaces, typed upstream events, registry, Python declarations, ROI selection, adapters | `output_capability_test.cpp`, `content_dependency_pipeline_test.py`, empty/invalid/unsupported regressions | Capability is not evidence of execution; Python output is declared rather than guessed |
| Examples and external protocols | Native packet/CLI examples, PythonScript/BYOM flow, caller-owned opaque payloads | Example and live bridge tests, explicit unsupported-payload comparison tests | BYOM external JSON remains caller-owned; it is not implicitly converted into SDK detections |

The authored-code audit found no remaining uses of the retired string-only
capability API. Old pose/performance names in `development/web/src/overlays.js`
are restricted to migration of saved browser preferences, with regression tests.
They do not decode old packets or select current measurements by retired conventions.
Existing box roles remain explicit filters within `BoxDetections`, not payload-type guesses.

## Executed checks

At the implementation snapshot, local validation includes 102 Meson targets,
135 SDK-tool tests, 47 Plumber tests, the Rust SDK tests and WebUI tests. The
geometry/two-tracker, gaze, mask, classification, performance and audio-context
pipeline checks were rerun with verbose output and executed without skips.
The live bridge tests cover representative optional values, large identities,
geometry, masks, embeddings, tracks and audio. Rust exercises the shared packet
and cross-language box fixture; this is not an exhaustive consumer test for every payload.

SDK/browser drift and configuration validation pass. Changed-line clang-tidy
completed with zero findings using the checked-in analysis owner. The initial
integration commit's CI findings were corrected in the owning commit; use the
current PR head when checking CI evidence. Documentation generation completes.
The execution-flow diagram was regenerated with the repository's PlantUML
1.2026.2 toolchain and now names `PerformanceMeasurementsT`.

CI also exposed a metadata-writer test ordering race: a streaming-source error can
precede the writer's specific reopen error. The owning integration PR now waits
within a fixed deadline for the required writer error, retaining source/message
assertions; repeated local runs pass. Runtime error behaviour is unchanged.

The integrated browser suite also passed eight real-Chrome tests, including DPR 2,
covering the pre-existing semantic palette/persistence, instance annotation
ordering/controls and OCR label/control cases. Added pixel cases cover a cropped
Uint32LE class map with an explicit void value and differently sized instance grids
whose overlap replaces rather than accumulates opacity. A wire-level OCR case
projects a normalized ROI subject without a parent-frame lookup or invented score.
These local overlay tests serve the built WebUI and inject displayed-frame metadata;
the full quick-start CI is intended to exercise the deployed pipeline on both architectures.
Earlier smoke attempts failed in dependency setup; this record does not claim
successful deployed coverage for the current heads. Use the PR checks for live status.

This evidence is not a claim of exhaustive manual screenshot parity or physical-device coverage.

### Review-driven boundary regressions

The deep review found a native OCR gap that the earlier independent parser and
browser fixtures did not exercise. `PaddleOcrPreprocess` now registers the actual
source frame and supplies its identity, dimensions and crop origin in both modes.
`DetectionBoxesFeedRecognitionWithTheSameSourceImageBasis` exercises the real
controller → Paddle preprocessor → DB parser → recognition crop controller →
recognition preprocessor path, with deterministic model outputs. It verifies
projectable boxes, nonzero crop offsets, the retained subject and one source-frame
record. This is a model-free boundary regression, not a claim of OCR model accuracy.

ImageNet, the OCR DB detector and the tracker now publish successful empty results.
Regressions cover filtered/uniform classifier output, blank OCR maps, unconfirmed
and expired tracks, and trace output being disabled. Empty classification metadata
does not paint a phantom native heading. Invalid data still fails without a
synthetic successful result.

Plumber's VAD/turn regression compares serialized windows with non-resident
derivation parents and retains producer/window/decision/score checks, including
nullable values and zero timestamps. The four native review reproductions and
the audio-parent comparison reproduction now pass; the fixes are folded into
their owning geometry, classification, tracking and shared-integration commits.

A subsequent review added two more boundary regressions. Plumber now checks the
cross-run track identity of derived subjects as well as their geometry. Reordered
records with new observation IDs/session UUIDs still match, while outcomes swapped
between coincident tracks fail for gaze, classification, contact, presence and
embedding results.

Instance-mask composition now uses bounded display/device-pixel resolution rather
than source-image resolution. The review's fine-grid crop that previously rendered
zero pixels now renders its expected visible cells. Browser regressions cover
paused-frame resize and DPR changes; unit checks also cover fractional DPR sizing,
cache reuse and allocation limits. Very large displays retain the documented
composition budget rather than claiming unbounded pixel-exact rendering.

### Generator review follow-up

PR #240 now reuses the image's checksum-locked FlatBuffers wheel when generating
the Rust packet fixture. Checksum verification and isolated Python execution
(`-I -S -B`) remain enabled; a corrupt supplied wheel fails validation, while an
absent image wheel retains the download fallback. The repository AGENTS link is restored.

`generate`, `check` and the fixture regressions pass with Docker `--network none`
on the foundation layer; `check` also passes offline on every upper layer.
The SDK-tool suite passes 135 tests with normal networking. Only the generation
receipt changes in the SDK snapshot; payload identities, APIs and fixture bytes are unchanged.

The bridge now follows flatc's enum-first namespace lookup. A generated C++
compilation regression covers a parent enum shadowed by a nearer table, including
scalar, vector, optional and struct fields, plus explicitly qualified table references.

### Performance identity and latest-main integration

Performance overlay sources retain the root producer through wire decoding and
saved selections. Replacing a publisher no longer matches its old selection.
Detailed timing IDs use complete, length-prefixed name hierarchies rather than
leaf labels; missing/truncated names leave identity unavailable. Regressions cover
same-name scopes, reordered records and delimiter collisions. Duplicate detection
includes unavailable timings and frame rates before display filtering, preventing
the reproduced 10/100 ms observations from incorrectly smoothing to 55 ms.

RetinaFace now publishes explicit image-space boxes and successful empty results,
and advertises typed face-box capabilities. Main's manual dependency-activation
control is preserved. The ResNet gaze pipeline test now checks explicit gaze
subjects and angle pairs. With the model assets installed by the existing download
owner, both resolutions and both ResNet backbones produced linked face/gaze results;
blank-frame handling and all live switching/reactivation cases passed on ONNX CPU.
This verifies integration, not model accuracy or physical-device performance.

## Package and version evidence

Product and packet identity are `1.0.0`. `tools/perception/sdk.json` intentionally
keeps `package_prerelease=true`: Python is `1.0.0.dev0`, Rust/TypeScript are
`1.0.0-dev.0`, and Plumber pins the matching development wheel. Configuration
format versions are independent; the OpChain reader is `1.10.0`. Main's RetinaFace
support owns `1.8.0`; embedding declarations and typed Python capabilities advance
the reader to `1.9.0` and `1.10.0`, respectively.

Two clean-tree builds from `a509f60f` used the existing checksum-locked cache at
`/opt/opk-deps/open-perception-kit-artifacts`, through `scripts/perception-sdk.sh`.
Both archives and required sidecars passed verification and produced the same ZIP hash:

```text
b98415384884f1da165ddace265cbf44a57e5b552b969b5f1deada943a8941f9
```

Local outputs are `artifacts/EXPKITS-1535-rereview-fixed/open-perception-kit-1.0.0.zip` and
`artifacts/EXPKITS-1535-rereview-fixed-repeat/open-perception-kit-1.0.0.zip`, each with `.sha256`
and `.provenance.json` sidecars. Provenance records the source commit and
`dirty=false`. Artifacts are not committed or published. Direct online fetching
encountered TLS failures; using the repository's existing locked artifact cache
completed the build without changing download ownership or integrity checks.
These archives predate the generator review follow-up and its refreshed receipt;
they are historical reproducibility evidence, not packages of the current source snapshot.

This verifies the **development SDK bundle**, not a stable product release or both
enclosing architecture archives. Release preparation must eventually clear the
development marker, regenerate all derived surfaces and run the existing release
package checks against the final integrated commit. No release workflow was dispatched.

## Completed integration and remaining gates

As checked on 2026-11-11, [#180 semantic overlays](https://github.com/arm/open-perception-kit/pull/180),
[#237 instance overlays](https://github.com/arm/open-perception-kit/pull/237) and
[#235 OCR overlays](https://github.com/arm/open-perception-kit/pull/235) are merged.
The metadata stack has been rebased onto main `95199ff9`, also including #272,
and the consumer fixes
are folded into the owning geometry, classification and mask layers:

- OCR projects declared box spaces, resolves `subject_id`, requires recognized-text
  kind, preserves unavailable scores, and retains bounded Unicode labels and controls.
- Semantic overlays read explicit encodings/label namespaces/void values and place
  model-sized or cropped grids without derivation-parent assumptions.
- Instance overlays retain optional annotations and unsigned/absent identities,
  reject mixed source frames/namespaces, and preserve overlap opacity and drawing order.
- Combined native, browser, SDK and Plumber checks have been rerun. Package evidence
  above remains tied to its earlier committed snapshot.

Peer review, current-head CI and bottom-up merging remain required. Infrastructure
resolution for the earlier Ubuntu package-host DNS failures is handed off.
Use current PR state for subsequent updates and changes to main.
Stable package preparation and final product-architecture evidence are separate
release gates. Development markers remain until release readiness is established.
Hardware and visual-validation limits above remain visible; no publication was performed.
