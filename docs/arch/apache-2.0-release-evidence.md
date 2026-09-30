---
sidebar_position: 16
sidebar_label: Apache-2.0 release evidence
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

# Apache-2.0 release evidence

Reviewed on 2026-09-30 against this branch's checked-in source and release
rules. This is a pre-publication check; no release artifact exists yet.

The binding redistribution conditions are in [Apache-2.0 Section 4](https://www.apache.org/licenses/LICENSE-2.0).
"Supported" means this branch already contains the files and release rules
needed to meet the condition when published. It does not mean publication has
occurred. Do not mark an implemented condition pending merely because an
artifact does not exist yet: the [release process](release-process.md#licence-evidence-before-publication)
assigns inspection of the selected commit and built artifacts to the release
reviewers before publication. "No applicable files found" means the reviewed
source has no file triggering that condition. This table covers checks software
developers and architects can perform; rights, approval, branding, and
contractual decisions require separate review.

| Requirement | Exact source | Evidence reviewed | Branch assessment |
| --- | --- | --- | --- |
| **OPK evidence control (Apache-2.0 Section 1): identify the material offered under OPK's Apache-2.0 licence.** | Evidence check derived from [Apache-2.0 Section 1](https://www.apache.org/licenses/LICENSE-2.0), definitions of Work and Licensor, and OPK's [README Licence section](../../README.md#licence). | [README](../../README.md#licence) and the [licensing page](../public/licensing.md) state the Apache-2.0 scope and third-party exceptions; the [component inventory](../third-party-licenses.md) lists third-party software. | **Supported.** The branch states the source licence scope and identifies third-party exceptions. |
| **Apache-2.0 Section 4(a): give every recipient a copy of Apache-2.0.** | [Apache-2.0 Section 4(a)](https://www.apache.org/licenses/LICENSE-2.0), licence-copy condition. | [Standalone Apache text](../../LICENSES/Apache-2.0.txt) is byte-identical to the [ASF text](https://www.apache.org/licenses/LICENSE-2.0.txt) (SHA-256 `cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30`); the root [LICENSE](../../LICENSE) ends with those same bytes. [ReleaseTool.py](../../scripts/release/ReleaseTool.py) stages `LICENSE` for architecture archives and images; the [SDK packager](../../tools/perception/package.py) requires `LICENSE` in the ZIP, and generated language packages contain it. | **Supported.** Source publication exposes the full text, and packaging rules carry it into release packages. |
| **Apache-2.0 Section 4(b): prominently mark files modified from an Apache-licensed work.** | [Apache-2.0 Section 4(b)](https://www.apache.org/licenses/LICENSE-2.0), modified-file notice condition. | The tracked [vendor tree](../../development/web/content/vendor/) has FlatBuffers licence text and Font Awesome assets, but no modified FlatBuffers source; [Meson subprojects](../../development/subprojects/) contain wraps and OPK build adapters, not patched upstream source. The [SDK generator](../../tools/perception/generate.py) produces files from OPK schemas, while the [SDK packager](../../tools/perception/package.py) copies Rust crates unchanged and adds separate checksum metadata. No tracked `.patch` or `.diff` file was found. | **No applicable files found in tracked source.** No modified file from Apache-licensed upstream work was identified. Reassess if a future change patches or vendors such source. |
| **Apache-2.0 Section 4(c): retain relevant copyright notices in distributed source-form derivatives.** | [Apache-2.0 Section 4(c)](https://www.apache.org/licenses/LICENSE-2.0), copyright-notice part of the source-form retention condition. | [REUSE.toml](../../REUSE.toml), adjacent `.license` files, and source headers record copyright; [ReleaseTool.py](../../scripts/release/ReleaseTool.py) collects original legal files and [SDK packaging](../../tools/perception/package.py) preserves vendored crate files byte-for-byte. | **Supported.** The branch preserves source copyright notices and stages original dependency notices. |
| **Apache-2.0 Section 4(c): retain relevant patent notices in distributed source-form derivatives.** | [Apache-2.0 Section 4(c)](https://www.apache.org/licenses/LICENSE-2.0), patent-notice part of the source-form retention condition. | [ReleaseTool.py](../../scripts/release/ReleaseTool.py) collects original dependency legal files; [SDK packaging](../../tools/perception/package.py) preserves vendored crate files byte-for-byte. | **Supported where applicable.** The branch retains original source and legal files rather than stripping patent notices. |
| **Apache-2.0 Section 4(c): retain relevant trademark notices in distributed source-form derivatives.** | [Apache-2.0 Section 4(c)](https://www.apache.org/licenses/LICENSE-2.0), trademark-notice part of the source-form retention condition. | [ReleaseTool.py](../../scripts/release/ReleaseTool.py) collects original dependency legal files; [SDK packaging](../../tools/perception/package.py) preserves vendored crate files byte-for-byte. | **Supported where applicable.** The branch retains original source and legal files rather than stripping trademark notices. |
| **Apache-2.0 Section 4(c): retain relevant attribution notices in distributed source-form derivatives.** | [Apache-2.0 Section 4(c)](https://www.apache.org/licenses/LICENSE-2.0), attribution-notice part of the source-form retention condition. | [ReleaseTool.py](../../scripts/release/ReleaseTool.py) copies original legal files and specified source files with embedded attributions; [SDK packaging](../../tools/perception/package.py) preserves vendored crate files byte-for-byte. | **Supported.** The branch stages original dependency notices and retained source attributions. |
| **Apache-2.0 Section 4(d): carry applicable attributions from an included Apache-licensed work's `NOTICE` file.** | [Apache-2.0 Section 4(d)](https://www.apache.org/licenses/LICENSE-2.0), first paragraph, applicable NOTICE attributions. | [ReleaseTool.py](../../scripts/release/ReleaseTool.py) copies upstream `NOTICE` files where present, including backend notices; the [SDK packager](../../tools/perception/package.py) requires its `NOTICE`. | **Supported.** The packaging rules preserve applicable upstream `NOTICE` text. |
| **Apache-2.0 Section 4(d): make required `NOTICE` attributions readable in an allowed location.** | [Apache-2.0 Section 4(d)](https://www.apache.org/licenses/LICENSE-2.0), first paragraph, permitted readable locations. | OPK's [NOTICE](../../NOTICE) is at the source repository root; the [generated SDK packages](../../generated/open_perception_kit/) each contain a `NOTICE`. The [licensing page](../public/licensing.md#notices-in-release-artifacts) identifies the readable notice locations in each release package. | **Supported.** The source repository already has a readable root `NOTICE`, and the packaging rules place notices in documented locations. |
| **Apache-2.0 Section 4(d): added `NOTICE` text must not modify Apache-2.0's terms.** | [Apache-2.0 Section 4(d)](https://www.apache.org/licenses/LICENSE-2.0), second paragraph, NOTICE text is informational and cannot modify the licence. | OPK's source [NOTICE](../../NOTICE) and the [SDK NOTICE](../../tools/perception/generator-inputs/NOTICE) contain attribution and dependency information rather than new licence terms. | **Supported.** The branch's authored `NOTICE` text does not change the Apache-2.0 terms. |

The source-side legal checks passed on this review date:
`python3 scripts/release/TestReleaseTool.py` (33 tests) and
`python3 tools/perception/tests/test_release.py -k licence` (1 test).

Apache-2.0's appendix recommends source notices but does not make a particular
header template a separate Apache-2.0 Section 4 condition. This table does not
establish contributor rights or patent grants under Apache-2.0 Sections 1–3,
trademark permission under Apache-2.0 Section 6, or any support or indemnity
decision under Apache-2.0 Section 9. It also does not clear
[third-party licences](../third-party-licenses.md), model or media rights, or
the project intellectual property review. Follow the [release process](release-process.md)
to reconcile those against the built artifacts and source software bill of
materials.
