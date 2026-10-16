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
