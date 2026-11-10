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

## [1.0.0] - Unreleased

### Changed

- Upgrade the FlowData generator to v0.7.1 and regenerate all OPK SDKs. Payload
  identities now depend on their qualified root names rather than schema contents.
- Adopt the four-state producer identity API. OPK consumers accept matching SDK
  names and major versions; schema hashes remain diagnostic information.
- Mark the SDK packages as development prereleases while the 1.0 refactor is in
  progress; the packet identity remains `1.0.0`.
- Make image coordinate spaces, optional category identity, classification kinds
  and subjects, contact/presence outcomes, gaze angles, mask encodings and meanings,
  embedding spaces, and track identity/history explicit in metadata.
- Publish structured performance measurements with producer/scope identity and
  independent measurement availability.
- Route model dependencies by payload root and optional role. Allow trusted Python
  Ops to declare typed inputs and outputs through the OpChain v1.10 reader extension.
- Preserve successful empty results separately from unavailable output. Expose audio
  source buffers and typed VAD/turn assessments across shared inspection and comparison.
- Integrate semantic/instance segmentation and OCR overlay controls with the 1.0
  contracts: declared mask regions/encodings/void values, optional annotations,
  explicit OCR subjects and recognized-text kinds.

### Fixed

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

### Breaking

- SDK 1.0 uses new payload identities and requires producers and consumers to be
  upgraded together. Earlier SDK packets are not supported. Payload shapes and
  processing semantics have changed; retire old pose/performance payloads and
  implicit string/parent-based interpretation rather than adding compatibility adapters.
- Replace string-only capability interfaces and control messages with typed output
  declarations. Custom Op libraries and WebUI clients must be upgraded together.
- Keep development package versions until stable release preparation is complete. See the
  [SDK 1.0 verification record](docs/arch/sdk-1.0-verification.md) for scope and limits;
  this entry does not authorize publication.

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
