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

# Changelog

All notable changes to this project are documented in this file, in
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) style. Versions
follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.1.4]

[Release-version exception](https://github.com/arm/open-perception-kit/pull/305#discussion_r4697027647):
OPK retains the planned 0.1.4 product version despite breaking metadata, SDK API
and configuration changes from 0.1.3. The independently versioned SDK is 1.0.0.
Upgrade producers,
consumers, custom Ops and configuration together; see the migration requirements
below.

### Added

- Rust crate README with a runnable consumer example, plus crates.io and offline
  installation guidance.
- WebUI Play/Pause/Stop controls and session-only model editing while stopped:
  add, replace, remove and reorder installed models within their groups, with
  Save/Discard and one editing browser at a time. Replacement rereads the OpChain
  and clears that stage's input connections, even when selecting the same path.
  See [browser model editing](docs/public/how-to/configure-elements.md#edit-models-from-the-browser).
- Optimized-Runtimes OR inference for stateless, static Float32 host tensors,
  with a YOLO26n example. Runtime libraries and model packages are provisioned
  separately.
- Configurable native log-mel audio preprocessing, generic voice-activity and
  keyword postprocessing, and overlapping VAD/turn windows. The voice demo
  supports custom pipelines and independent keyword assessments.
- Checksum-verified dependency patches with packaging receipts.
- Nightly validation of builds, installed packages, archives and real inference,
  with health reports for nightly and release runs.

### Changed

- Upgrade the FlowData generator to v0.7.1 and regenerate all OPK SDKs. Payload
  identities now depend on their qualified root names rather than schema contents.
- Adopt the four-state producer identity API. OPK consumers accept matching SDK
  names and major versions; schema hashes remain diagnostic information.
- Version SDK packets and packages independently of the OPK product release.
  Set SDK package versions to 1.0.0, replacing the intermediate development
  versions; the packet identity remains `1.0.0`. Reuse an identical published
  SDK crate across OPK releases without publishing it again.
- Make image coordinate spaces, optional category identity, classification kinds
  and subjects, contact/presence outcomes, gaze angles, mask encodings and meanings,
  embedding spaces, and track identity/history explicit in metadata.
- Publish structured performance measurements with producer/scope identity and
  independent measurement availability.
- Connect model inputs to concrete producers, using SDK payload roots for compatibility.
  Allow explicit live input selection and typed Python input/output declarations.
- Preserve successful empty results separately from unavailable output. Expose audio
  source buffers and typed VAD/turn assessments across shared inspection and comparison.
- Integrate semantic/instance segmentation and OCR overlay controls with the 1.0
  contracts: declared mask regions/encodings/void values, optional annotations,
  explicit OCR subjects and recognized-text kinds.
- Migrate saved overlay settings from v2 to v3, retaining explicit renderers,
  source identities and settings while removing retired selectors.
- Create editable default browser overlays in fresh setups and preserve saved
  styles, visibility and removals. Move `full-onnx-yuv` decorations from native
  `opkosd` into browser overlays.
- Unify compact sidebar and overlay switches while preserving larger hit areas
  and keyboard focus after model toggles.
- Retain browser VAD and turn results between inference windows, clearing them
  when the source resets or stops.
- Use the official NuGet package as the default ONNX Runtime download source;
  retain the original GitHub source through `OPK_ONNXRUNTIME_SOURCE=github`.
- Update the manual PythonScript installation pin in `requirements/python-ops.txt`
  to NumPy 2.5.5. The standard runtime setup continues to use NumPy 2.4.2 from
  `development/ops-python/runtime.json`.
- Unify documentation generation and validate prepared publication sources.

### Fixed

- Check the development container checkout mount before building; request recreation
  for a different checkout and reject a still-mismatched mount.

- Include every configuration schema version in release packages, including the
  v2 Model and OpChain schemas required by the current runtime.
- Reconnect browser media and dynamic decoder links after Stop, and retain the
  editor for correction after recoverable pipeline errors. Failed edits remain
  available for correction; earlier applied edits are not rolled back.
- Keep model-group identities unique across nested bins and reject stale edits
  while model registrations update.
- Correct category-zero handling, non-finite measurement handling, subject and
  producer ambiguity, gaze angular wrap comparison, and embedding compatibility checks.
- Preserve mask placement/opacity semantics, elapsed-time track history and valid
  display labels while rejecting invalid or contradictory metadata.
- Prevent unsupported or malformed payloads from silently producing successful
  comparisons, and wait for asynchronous metadata publication in finite pipeline tests.
- Supply PaddleOCR's source-image basis through detection and recognition preprocessing;
  preserve successful empty classifier/detector/tracker results without phantom headings,
  and compare self-contained audio windows independently of retained derivation parents.
- Preserve fine instance-mask cells at display resolution and refresh composition
  on resize/DPR changes. Match derived track subjects by identity as well as geometry
  so coincident tracks cannot hide swapped outcomes in regression comparisons.
- Reduce browser background-segmentation flicker by retaining matched overlays
  for up to 100 ms after the first metadata miss and enabling TCP_NODELAY.
  Retained masks can briefly lag moving video.
- Extend browser regression coverage for mask opacity, retention expiry and
  exact RTP metadata matching across timestamp wraparound.
- Clarify independent microphone capture and audio playback mute controls,
  synchronize playback labels with volume changes, and prevent sidebar clipping.
- Process browser model toggles in arrival order without blocking the control
  thread, and prevent tracker trace-retention overflow.
- Restore Plumber keyword comparisons and convert UltraFace logits to
  probabilities before filtering detections.
- Correct generated C++ type lookup when enum and table names collide across
  namespaces.

### Breaking

- SDK 1.0 uses new payload identities and requires producers and consumers to be
  upgraded together. Earlier SDK packets are not supported. Payload shapes and
  processing semantics have changed; retire old pose/performance payloads and
  implicit string/parent-based interpretation rather than adding compatibility adapters.
- Replace string-only capability interfaces and control messages with typed output
  declarations. Custom Op libraries and WebUI clients must be upgraded together.
- Remove `LayerInfo.content_type`, Model `contentType`, semantic capability roles
  and subtype-based ROI/tracker/rendering selection. Model and OpChain formats move
  to `2.0.0`; migrate sources explicitly. See
  [explicit result inputs](docs/arch/explicit-input-connections.md).
- Rename the inference dependency policy to `auto-activate-model-dependencies`;
  require GLib 2.70 or newer.
- Allow only one `opksink` per pipeline, including nested bins. Pipelines with
  multiple instances must remove extra sinks or use separate pipelines.

See the [SDK 1.0 verification record](docs/arch/sdk-1.0-verification.md) for the
refactor's validation scope and limits.

## [0.1.3]

This release includes new functionality under the approved 0.1.3 version
exception for [EXPKITS-1570](https://jira.arm.com/browse/EXPKITS-1570).
The general Semantic Versioning policy remains unchanged.

### Added

- Browser microphone and webcam input through `opksrc`, with independent capture
  controls.
- PCM audio inference, Silero voice activity detection, SmartTurn turn assessment,
  and a voice pipeline example, with generated SDK payloads for audio results.
- Linear DMA-BUF camera input and forwarding, plus optional Vulkan image
  preprocessing.
- Optional ML Workload Library inference for VGF models, with UltraFace and YOLO26
  examples and a CPU emulation quick start.
- YOLO26 semantic and instance segmentation postprocessors, FrameResults payloads,
  and configurable browser overlays.
- PP-OCRv6 text detection and recognition, pinned model and dictionary downloads,
  and model-independent OCR overlays.
- RetinaFace face detection and ResNet34/50 gaze estimation pipelines on ONNX
  Runtime CPU.
- Configurable WebUI overlays for detections, tracking, movement trails,
  classification, gaze, camera contact, performance, and background replacement.
- Runtime and demo Debian packages for `amd64` and `arm64`, published alongside
  the architecture archives to Artifactory and GitHub Releases.

### Changed

- Define supported mutation states for GStreamer properties, retain live controls,
  and document when a restart is required; remove the ineffective
  `opkinfer.format` property.
- Make ONNX intra-op and inter-op thread counts configurable, tune Raspberry Pi
  presets, and cap default thread counts by available CPUs.
- Update ONNX Runtime to 1.31.1, refresh other dependencies and their licence
  inventory, and include MLWL in release packages.
- Allow deployment compile-job overrides and improve x86-to-ARM64
  cross-compilation and emulated ARM64 builds.

### Fixed

- Synchronize browser overlays with displayed WebRTC video frames, including
  metadata that arrives after its video frame.
- Allow transient ICE disconnections to recover before reconnecting, improve
  WebRTC diagnostics, and refresh stale TURN addresses in devcontainers.
- Discover metadata WebSocket ports and endpoints from the pipeline, with an
  explicit port override.
- Reduce VP8 encoding CPU usage on Raspberry Pi 5.
- Release inactive browser microphone sessions and previous devcontainer port
  bindings.
- Preserve OpChain setup and execution failure details in GStreamer bus errors.
- Reject request bodies on bodyless HTTP routes.

## [0.1.2]

### Changed

- Smooth WebUI performance readings to reduce fluctuations in displayed metrics.
- Allow quick-start setup on other Arm64 Linux hosts, with a warning that these
  hosts are untested; require Linux and APT for Raspberry Pi setup.
- Clone the latest released source in setup guides. Successful releases now move
  the `latest_release` tag after documentation publication completes.
- Update build and Python dependencies and their third-party licence inventory.
- Run CI on Ubuntu 24.04, enable trusted crates.io publishing, and remove retired
  analysis, secret-scanning, and agent tooling.

### Fixed

- Restore devcontainer builds by removing unnecessary package version pins while
  retaining and documenting compatibility-sensitive versions.
- Open devcontainers in the current checkout and release conflicting OPK
  devcontainer ports before startup.
- Check for Docker Buildx before quick-start builds and install it on supported
  APT profiles; clarify platform storage requirements.
- Retry artifact downloads after TLS connection failures.
- Skip optional demo-video downloads when `HF_TOKEN` is absent or empty. Setup
  guides explain how to supply input videos; browser smoke tests use a generated
  ball video source instead of downloaded demo media.
- Reject already-published release versions before building or publishing artifacts.

## [0.1.1]

Initial release
