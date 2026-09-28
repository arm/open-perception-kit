<!--
SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

# Third-party notices

OPK-owned code is Apache-2.0. The components below retain their original
copyrights and licence terms. The SPDX identifier above covers this inventory,
not the third-party works it describes.

Architecture packages carry the original texts and `components.json` under
`share/opk/licenses/`. Deployment and Cairn images use `/share/opk/licenses/`.
`THIRD_PARTY_LICENSES.md` provides a report for the packaged OPK version, with
links to each original notice. `components.json` records the actual source
revision or installed version, repository, licence, and every collected notice
path. Both are generated from the same collection. The collection also includes build
notices; inclusion of a notice does not imply that every component is used by
that particular executable. See the [licence page](docs/public/licensing.md).

The [TPIP report](https://github.com/arm/open-perception-kit/blob/main/docs/third-party-licenses.md)
preserves the supplied Black Duck SBOM snapshot and its review status. It includes
1,111 component/version entries across the scanned environment. Its scan revision
is recorded separately from the versions collected for a release below.

## Native components

The source pins are maintained in `development/subprojects/*.wrap`.
The notice collector checks versions against
[`scripts/release/third-party-licenses.json`](scripts/release/third-party-licenses.json).
When a dependency changes, reconcile its original notices and update the
catalogue and this overview together before packaging.
The collector copies original licence and notice files recursively, including
notices for dependencies embedded in these projects and Meson build patches.

| Component | Version or source revision | Licence | Original notice | Surfaces |
| --- | --- | --- | --- | --- |
| [Asio](https://think-async.com/Asio/) | 1.30.2 | BSL-1.0 | `asio/COPYING`, `asio/LICENSE_1_0.txt` | Architecture packages, deployment, Cairn |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | 0.56.0 | MIT | `cpp-httplib/LICENSE` | Architecture packages, deployment, Cairn notice collection |
| [fmt](https://github.com/fmtlib/fmt) | 12.0.0 | MIT | `fmt/LICENSE` | Architecture packages, deployment, Cairn |
| [GoogleTest](https://github.com/google/googletest) | 1.17.0 | BSD-3-Clause | `gtest/LICENSE` | Build and test dependency; notice collection |
| [jsoncons](https://github.com/danielaparker/jsoncons) | cb54cdc3134a62634466bf7bcd24f1a906f4ef25 | BSL-1.0 AND MIT | `jsoncons/LICENSE`, `jsoncons/include/jsoncons/detail/grisu3.hpp` (Florian Loitsch attribution and MIT terms) | Architecture packages, deployment, Cairn |
| [magic_enum](https://github.com/Neargye/magic_enum) | 0.9.7 | MIT | `magic_enum/LICENSE` | Architecture packages, deployment, Cairn |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.12.0 | MIT | `nlohmann_json/LICENSE.MIT`, `nlohmann_json/include/nlohmann/detail/conversions/to_chars.hpp` (Florian Loitsch attribution) | Architecture packages, deployment, Cairn |
| [stb](https://github.com/nothings/stb) | 2c980bb59875b0d32144a71867fbdebb2f77cd20 | MIT OR Unlicense | `stb/LICENSE` | Architecture packages, deployment, Cairn |
| [tl::expected](https://github.com/TartanLlama/expected) | 1.3.1 | CC0-1.0 | `tl-expected/COPYING` | Architecture packages, deployment, Cairn |
| [WebSocket++](https://github.com/zaphoyd/websocketpp) | 0.8.2 | BSD-3-Clause, Zlib, MIT for bundled code | `websocketpp/COPYING` | Architecture packages, deployment, Cairn notice collection |

WebSocket++'s complete `COPYING` includes the notices for its Base64, SHA1,
MD5, and UTF-8 code. The stb licence preserves both upstream alternatives.
Upstream licences and copyright statements are copied without replacing
contact addresses or asserting Arm ownership.

## Backends and SDK runtimes

| Component | Version | Licence | Original notice | Surfaces |
| --- | --- | --- | --- | --- |
| [ONNX Runtime](https://github.com/microsoft/onnxruntime) | 1.24.4 | MIT; bundled dependencies have separate terms | `onnxruntime/LICENSE`, `onnxruntime/ThirdPartyNotices.txt` | Architecture packages, deployment, Cairn |
| [ExecuTorch](https://github.com/pytorch/executorch) | 1.3.1 | BSD-3-Clause; bundled dependencies have separate terms | `executorch/` preserves the complete legal tree from the SDK package | Architecture packages and deployments that include the backend |
| [FlatBuffers](https://github.com/google/flatbuffers) | 25.9.23 | Apache-2.0 | `flatbuffers/LICENSE`; original wheel/npm/crate notices | Native artifacts, browser UI, SDK |
| [NumPy](https://numpy.org/) | 2.4.2 | BSD-3-Clause; bundled libraries have separate terms | `python-numpy/` preserves wheel licences, including its bundled-library notices | Architecture packages and Python-enabled deployment runtime |
| [bitflags](https://crates.io/crates/bitflags) | 2.13.1 | MIT OR Apache-2.0 | Original crate and `rust/vendor/bitflags-2.13.1/` | SDK Rust runtime |
| [rustc_version](https://crates.io/crates/rustc_version) | 0.4.1 | MIT OR Apache-2.0 | Original crate and `rust/vendor/rustc_version-0.4.1/` | SDK Rust runtime |
| [semver](https://crates.io/crates/semver) | 1.0.28 | MIT OR Apache-2.0 | Original crate and `rust/vendor/semver-1.0.28/` | SDK Rust runtime |

The SDK ZIP preserves whole pinned runtime wheels, npm archives and Rust crates,
as well as the Rust vendor tree. Per-language OPK packages carry `LICENSE` and
`NOTICE`; the ZIP also includes the licence page. HailoRT and NCNN are not in the
current release payload. A backend added to the payload requires its original
licence and complete dependency notices before publication.

## Browser assets and media

[Font Awesome Free 6.5.2](https://github.com/FortAwesome/Font-Awesome/tree/6.5.2)
provides `development/web/content/vendor/fontawesome/css/all.min.css` (MIT) and
`webfonts/fa-solid-900.woff2` (OFL-1.1). Its unmodified `LICENSE.txt` is alongside
those files and in `fontawesome/` in the release notice collection. The upstream
file also describes SVG/JS icon terms; those icon formats are not vendored here.
The FlatBuffers browser runtime's original `LICENSE` is also kept alongside the
WebUI in `development/web/content/vendor/flatbuffers/`.

Getty images in `data/images/` and videos identified by
`scripts/private/demo-videos.manifest` are third-party media. They are not
covered by OPK's Apache licence. Their redistribution terms must be supplied in
`data/GETTY-LICENSE.txt`; image release validation rejects missing terms. The
media remains in the project while these terms are being confirmed.

Model binaries are not distributed in OPK releases. Users download the pinned
models with their own Hugging Face access through the existing quick-start
flow. Each model's upstream terms remain applicable to that separate download.

## Container and host libraries

Containers retain Debian's original `/usr/share/doc/<package>/copyright` and
`/usr/share/common-licenses/` files. `/share/opk/licenses/debian-packages.tsv`
records the exact installed package versions, including GStreamer, GLib,
libsoup, libnice, OpenSSL and their dependencies. These system libraries retain
their upstream terms, which include licences other than Apache-2.0. Architecture
packages require the documented host GStreamer and system-library installation;
they do not incorporate that installation into the OPK tarball.
The deployment image's CPython 3.14.7 installation also retains its original
PSF and incorporated-component terms in `/usr/local/lib/python3.14/LICENSE.txt`.

A successful build or this inventory does not establish IP review approval.
Release preparation must reconcile the actual artifact with the scan and record
the required approval before publication.
