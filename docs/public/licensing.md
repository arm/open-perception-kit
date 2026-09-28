---
title: Licensing
sidebar_label: Licensing
sidebar_position: 13
description: Open Perception Kit licence, copyright, and third-party notices.
---
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

# Licensing

Copyright 2025-2026 Arm Limited and/or its affiliates
[perception-fdbck@arm.com](mailto:perception-fdbck@arm.com).

Open Perception Kit is licensed under the Apache License, Version 2.0, except
where a file or its accompanying notice identifies different terms. The full
licence and an overview of its scope are in the repository's
[LICENSE](https://github.com/arm/open-perception-kit/blob/main/LICENSE).
[LICENSES/Apache-2.0.txt](https://github.com/arm/open-perception-kit/blob/main/LICENSES/Apache-2.0.txt)
contains the standalone licence text.

Source headers combine the copyright notice, `SPDX-License-Identifier` and
the Apache short notice, including its licence URL and warranty disclaimer.
Files whose contents must stay unchanged, such as the schemas used to derive
SDK payload identities, use adjacent `.license` files. `REUSE.toml` associates
licensing information with JSON, documentation images, and test fixtures.
Keep these notices with the files when copying or redistributing them.

Third-party components retain their own licences and copyright notices.
The project licence does not grant rights to third-party model weights,
photographs, video, fonts, or other content.

## Notices in release artifacts

- Architecture packages: `share/opk/licenses/README.md`, the original texts,
  and `THIRD_PARTY_LICENSES.md` in the same directory. This report lists each
  collected component's version, repository, licence and local notice files;
  `components.json` contains the same information in machine-readable form.
- Deployment and Cairn images: `/share/opk/licenses/`. System package notices
  remain in `/usr/share/doc/` and `/usr/share/common-licenses/`; the installed
  package versions are recorded in `debian-packages.tsv` in the OPK notice directory.
- SDK ZIP: `LICENSING.md`, `LICENSE`, and `NOTICE` at the root. The SDK's Python
  wheels, TypeScript archives and Rust vendor tree retain original runtime notices.
  Each OPK language package also includes its own `LICENSE` and `NOTICE`.
- Source checkout: the [third-party inventory](https://github.com/arm/open-perception-kit/blob/main/THIRD_PARTY_NOTICE.md)
  identifies projects, versions, licences and where their original notices are kept.

Font Awesome's CSS is MIT-licensed and its bundled font uses OFL-1.1. Their
original `LICENSE.txt` accompanies the files under `development/web/content/vendor/fontawesome/`.
FlatBuffers' Apache-2.0 text is under `development/web/content/vendor/flatbuffers/`.

Model binaries are downloaded separately using the user's Hugging Face access;
they are not included in releases.

For OPK questions, contact [perception-fdbck@arm.com](mailto:perception-fdbck@arm.com).
