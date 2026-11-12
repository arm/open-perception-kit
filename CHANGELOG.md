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
