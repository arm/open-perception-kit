---
title: Use an OPK Binary Release
sidebar_position: 3
sidebar_label: Binary Release
description: Install and integrate an OPK architecture package without an OPK-specific loader wrapper.
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


# Use an OPK binary release

Each release contains exactly three archives:

- `opk-<version>-linux-x86_64.tar.gz`
- `opk-<version>-linux-aarch64.tar.gz`
- `opk-docs-<version>.tar.gz`

Choose the architecture archive that matches `uname -m`. The documentation
archive is architecture-neutral and contains the offline public site in
`html/` and the generated API reference in `doxygen/`.

The same native builds can be downloaded from the commandline as well:

```bash
gh auth login
gh release download <version> --repo arm/open-perception-kit --pattern 'opk-<version>-linux-<architecture>.tar.gz'
```

Release notes provide the immutable image digest. Use the digest-qualified
reference when a deployment must remain pinned. The image keeps the existing
`opk-deployment-base` entrypoint and selects its pipeline through
`OPK_PIPELINE`; provide the networking, ports, and devices required by that
pipeline when creating the container.

Verify the hash shown beside the archive on the GitHub Release or Artifactory
workflow summary before extracting it:

```bash
sha256sum opk-<version>-linux-<architecture>.tar.gz
```

Architecture packages contain the six OPK plugins, the private
`lib/opk/opk-runtime.so` and common libraries, model descriptors and
OpChains, `opksink` web assets, approved notices, and ONNX Runtime. They also
contain the experimental ExecuTorch and PythonScript operation modules, plus
the SDK and descriptor schemas:

- `share/opk/open-perception-kit/` contains the open-perception-kit ZIP, checksum, and
  provenance sidecar;
- `share/opk/schemas/json/v1/` contains the model and OpChain descriptor JSON
  schemas copied from the released source.

The descriptor schemas are direct OPK package content, separate from the SDK
ZIP. The archive includes the PythonScript type stub under `share/opk/python`;
PythonScript's Python packages are installed in a host environment.

They deliberately exclude model binaries, `opk-menu`, pipeline presets, examples, sample
media, documentation, source, tests, debug files, public C++ headers,
unused ONNX provider libraries, and accelerator drivers or firmware.
ExecuTorch SDK headers and static libraries
are build inputs and are not exposed by the archive.

## Host prerequisites

OPK packages target Debian Trixie. Install the system GStreamer runtime and
tools with the Base, Good, and Bad plugin sets, including Nice and the WebRTC
plugins. GLib, OpenSSL, zlib, Brotli, zstd, libsoup 3, json-glib, the
C/C++ runtimes, and any required accelerator driver and firmware remain host
dependencies. The Arm package also requires the system `libusb-1.0` runtime.
PythonScript OpChains require Python 3.13 and its shared library. From a
checkout at the release's source revision, install the Python runtime
dependencies into a virtual environment and point OPK at it:

```bash
./scripts/setup-python-ops-runtime.sh --python python3.13 \
  --venv "$PWD/.venv-python-ops" \
  --perception-sdk generated/open_perception_kit/python
export OPK_PYTHON_RUNTIME_VENV="$PWD/.venv-python-ops"
```

The setup script installs the versions recorded in the runtime and SDK
descriptors. The virtual environment stays outside the extracted archive.
Python applications that drive GStreamer directly also need the system
PyGObject bindings, available as `python3-gst-1.0`.

## Extract and discover the plugins

Extract the archive anywhere readable, then set only `GST_PLUGIN_PATH`:

```bash
tar -xzf opk-<version>-linux-<architecture>.tar.gz
export GST_PLUGIN_PATH="$PWD/opk-<version>-linux-<architecture>/lib/gstreamer-1.0"
gst-inspect-1.0 opkinfer
```

Do not set an OPK-specific `LD_LIBRARY_PATH`. Each plugin and private runtime
library uses a package-relative RUNPATH.

`opk-runtime.so` is a private dependency of the packaged plugins, not a public
C++ SDK. Cairn integrates the package through standard GStreamer APIs and does
not compile against OPK headers.

The package has one ONNX Runtime binary, `libonnxruntime.so.1.24.4`.
`libonnxruntime.so.1` is only a symbolic link to that file, not a second
runtime. The link is required because the runtime's upstream SONAME is recorded
as `DT_NEEDED=libonnxruntime.so.1` in `opk-onnx-ops.so`; the dynamic loader
looks up that exact name.

For source and licence review, the archive keeps third-party notices under
`share/opk/licenses/`. The ONNX Runtime library also has an adjacent
`.provenance.json` recording its download and file hashes. ExecuTorch source
identity is included with its notices when provided by the build input.

ExecuTorch is statically linked into `lib/opk/opk-executorch-ops.so`. It remains
experimental and does not add a public SDK surface to the binary release.

The plugin directory contains the six supported plugins:

- `libopkcomm.so`
- `libopkinfer.so`
- `libopkosd.so`
- `libopkperformance.so`
- `libopksink.so`
- `libopktracker.so`

## Integrate a Cairn GStreamer backend

Cairn uses its existing GStreamer API to construct pipelines containing OPK
elements. Point `GST_PLUGIN_PATH` at the extracted plugin directory before
initializing GStreamer; the plugins locate `opk-runtime.so` and the other
private libraries through their package-relative RUNPATHs.

The package exposes no OPK C++ headers and Cairn does not link directly to
`opk-runtime.so`. The `opkperformance` element remains available as a normal
GStreamer element, but the OPK C++ performance-metrics API is not part of the
binary release.

To consume serialized `FrameResults`, Cairn can verify and extract the nested
the Open Perception Kit SDK with the matching release tooling:

```bash
sdk_root="$OPK_PACKAGE_ROOT/share/opk/open-perception-kit"
./scripts/perception-sdk.sh verify \
  "$sdk_root/open-perception-kit-<opk-version>.zip" \
  --require-sidecars
unzip "$sdk_root/open-perception-kit-<opk-version>.zip" -d open-perception-kit
```

Use the C++, Python, Rust, or TypeScript package from that extracted SDK. The SDK
version matches the OPK product version.

## Download models

Releases contain model descriptors and OpChains, with pinned `hfDownload`
metadata, but no model binaries. Download the models using your own Hugging Face
access before running inference. Private or gated repositories require a
read-only `HF_TOKEN` with access to those models.

The existing quick-start setup downloads models into the checkout's
`config/models` directory. Use a checkout at the release's source revision and
set `HF_TOKEN` in your host shell before running `./scripts/quick_start.sh`.
The local development image uses that token only during its model-download
build step and seeds the downloaded files into the checkout at startup.

For an extracted binary package, reuse the same downloader to populate the
package's model directory. Put the extracted package under the checkout's
`var/` directory so it is accessible in the quick-start container, then run
from the host shell with `HF_TOKEN` still exported:

```bash
docker exec -u dev -e HF_TOKEN open-perception-kit \
  python3 /work/scripts/download-models.py \
  --models-dir /work/var/opk-<version>-linux-<architecture>/share/opk/models
```

Downloads are checked against the descriptor's SHA-256. Failed or inaccessible
downloads are reported and skipped; check the output before starting inference.
Inference then uses the local files and needs no Hugging Face token.

| Package | Backend | Descriptor directories |
| --- | --- | --- |
| x86_64 and Arm | ONNX | `mobilegaze-mobilenet-v2`, `nitec-resnet-18`, `osnet-x0-25`, `ultraface-rfb-320`, and the six `yolo26{n,s}-{320,480,640}` variants |
| x86_64 and Arm | ExecuTorch | `mobilegaze-mobilenet-v2-executorch`, `nitec-resnet-18-executorch` |

Deployment and Cairn images also contain no model binaries. The top-level
`compose.yaml` mounts the checkout's `config/models` into the deployment
container. Download the models through quick start first. When creating a
container directly, mount the downloaded model directories at
`/work/config/models`; Cairn needs `yolo26n-320`.

## Run packaged inference

After downloading the models, descriptors, binaries, and compatible OpChains
are under `share/opk`. For example:

```python
import os
from pathlib import Path

import gi

gi.require_version("Gst", "1.0")
from gi.repository import Gst

package_root = Path(os.environ["OPK_PACKAGE_ROOT"])
os.environ["GST_PLUGIN_PATH"] = str(package_root / "lib/gstreamer-1.0")
Gst.init(None)

pipeline = Gst.parse_launch(
    "videotestsrc num-buffers=30 ! videoconvert ! "
    "video/x-raw,format=BGRA ! opkinfer name=infer ! fakesink"
)
pipeline.get_by_name("infer").set_property(
    "opchain-path", str(package_root / "share/opk/models/yolo26n-320/opchain.json")
)
pipeline.set_state(Gst.State.PLAYING)
message = pipeline.get_bus().timed_pop_filtered(
    Gst.CLOCK_TIME_NONE, Gst.MessageType.ERROR | Gst.MessageType.EOS
)
pipeline.set_state(Gst.State.NULL)
if message.type == Gst.MessageType.ERROR:
    raise RuntimeError(message.parse_error()[0].message)
```

`opksink` serves the packaged browser UI from `web/content` by default. Its
`static-files` property remains available when an application needs an explicit
web root. Top-level `opk-menu` pipeline presets and sample media are
intentionally not part of the binary release.

The same smoke path is run natively for x86_64 and Arm packages on pull
requests targeting `main`. Pushes to `main` publish the three matching archives
on one GitHub Release and together in generic Artifactory under
`releases/<version>/`, the open-perception-kit wheel to Artifactory PyPI, the Open Perception Kit
crate to Artifactory Cargo, and the matching multi-architecture image in GHCR.
Manual runs publish the archives, wheel, and crate only to generic Artifactory
under `snapshots/<label>/<full-sha>-<run-id>-<attempt>/`. The generic archive
and snapshot paths use
`https://artifactory.arm.com/artifactory/ai-expkits-internal.opk-ci` as their
base URL. The final Artifactory workflow log and job summary contain the folder,
links, hashes, and the matching run-unique GHCR snapshot reference.

[Back to Getting Started](/getting-started)
