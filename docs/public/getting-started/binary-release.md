---
title: Use a PEK Binary Release
sidebar_position: 3
sidebar_label: Binary Release
description: Install and integrate a PEK architecture package without a PEK-specific loader wrapper.
---

# Use a PEK binary release

Each release contains exactly three archives:

- `pek-<version>-linux-x86_64.tar.gz`
- `pek-<version>-linux-aarch64.tar.gz`
- `pek-docs-<version>.tar.gz`

Choose the architecture archive that matches `uname -m`. The documentation
archive is architecture-neutral and contains the offline public site in
`html/` and the generated API reference in `doxygen/`.

The same native builds are also available as one multi-architecture runnable
image. Docker selects the matching amd64 or arm64 manifest automatically:

```bash
docker pull ghcr.io/arm-debug/amp-dev-forge-deployment:<version>
```

Release notes provide the immutable image digest. Use the digest-qualified
reference when a deployment must remain pinned. The image keeps the existing
`pek-deployment-base` entrypoint and selects its pipeline through
`PEK_PIPELINE`; provide the networking, ports, and devices required by that
pipeline when creating the container.

Verify the hash shown beside the archive on the GitHub Release or Artifactory
workflow summary before extracting it:

```bash
sha256sum pek-<version>-linux-<architecture>.tar.gz
```

Architecture packages contain the six PEK plugins, the private
`lib/pek/pek-runtime.so` and common libraries, compatible model binaries and
OpChains, `peksink` web assets, approved notices, and ONNX Runtime. They also
contain the experimental ExecuTorch operation module, the PythonScript operation
module, the YOLOX ExecuTorch model, and a private locked Python package directory
at `share/pek/python`, plus these two distinct payloads:

- `share/pek/perception-sdk/` contains the Perception SDK ZIP, checksum, and
  provenance sidecar;
- `share/pek/schemas/json/v1/` contains the model and OpChain descriptor JSON
  schemas copied from the released source.

The descriptor schemas are direct PEK package content, not files in the SDK
ZIP. Retired `metadata/api` schemas are not included.

They deliberately exclude `pek-menu`, pipeline presets, examples, sample
media, documentation, source, tests, debug files, public C++ headers,
unused ONNX provider libraries, and accelerator drivers or firmware.
ExecuTorch SDK headers and static libraries
are build inputs and are not exposed by the archive.

## Host prerequisites

PEK packages target Debian Trixie. Install the system GStreamer runtime and
tools with the Base, Good, and Bad plugin sets, including Nice and the WebRTC
plugins. GLib, Cairo, OpenSSL, zlib, Brotli, zstd, libsoup 3, json-glib, the
C/C++ runtimes, and any required accelerator driver and firmware remain host
dependencies. The Arm package also requires the system `libusb-1.0` runtime.
PythonScript OpChains require the Debian Trixie `python3` and `libpython3.13`
packages; NumPy, FlatBuffers, and the Perception guest package are already
included privately in the PEK archive. Python applications that drive
GStreamer directly also need the system PyGObject bindings, available as
`python3-gst-1.0`.

## Extract and discover the plugins

Extract the archive anywhere readable, then set only `GST_PLUGIN_PATH`:

```bash
tar -xzf pek-<version>-linux-<architecture>.tar.gz
export GST_PLUGIN_PATH="$PWD/pek-<version>-linux-<architecture>/lib/gstreamer-1.0"
gst-inspect-1.0 pekinfer
```

Do not set a PEK-specific `LD_LIBRARY_PATH`. Each plugin and private runtime
library uses a package-relative RUNPATH.

`pek-runtime.so` is a private dependency of the packaged plugins, not a public
C++ SDK. Cairn integrates the package through standard GStreamer APIs and does
not compile against PEK headers.

The package has one ONNX Runtime binary, `libonnxruntime.so.1.24.4`.
`libonnxruntime.so.1` is only a symbolic link to that file, not a second
runtime. The link is required because the runtime's upstream SONAME is recorded
as `DT_NEEDED=libonnxruntime.so.1` in `pek-onnx-ops.so`; the dynamic loader
looks up that exact name.

ExecuTorch is statically linked into `lib/pek/pek-executorch-ops.so`. It remains
experimental and does not add a public SDK surface to the binary release.

The plugin directory contains the six supported plugins:

- `libpekcomm.so`
- `libpekinfer.so`
- `libpekosd.so`
- `libpekperformance.so`
- `libpeksink.so`
- `libpektracker.so`

## Integrate a Cairn GStreamer backend

Cairn uses its existing GStreamer API to construct pipelines containing PEK
elements. Point `GST_PLUGIN_PATH` at the extracted plugin directory before
initializing GStreamer; the plugins locate `pek-runtime.so` and the other
private libraries through their package-relative RUNPATHs.

The package exposes no PEK C++ headers and Cairn does not link directly to
`pek-runtime.so`. The `pekperformance` element remains available as a normal
GStreamer element, but the PEK C++ performance-metrics API is not part of the
binary release.

To consume serialized `FrameResults`, Cairn can verify and extract the nested
Perception SDK with the matching release tooling:

```bash
sdk_root="$PEK_PACKAGE_ROOT/share/pek/perception-sdk"
./scripts/perception-sdk.sh verify \
  "$sdk_root/perception-sdk-<pek-version>.zip" \
  --require-sidecars
unzip "$sdk_root/perception-sdk-<pek-version>.zip" -d perception-sdk
```

Use the C++, Python, Rust, or TypeScript package from that extracted SDK. The SDK
version matches the PEK product version.

## Packaged models

All packaged model references are local. During release creation, pinned
`hfDownload` metadata from the selected source commit is resolved once and the
model binaries are packaged with their JSON configuration. The model descriptor
defines its tensor shape, and the accompanying OpChain converts the pipeline's
BGRA video frame into that input. Release users need neither network access nor
a Hugging Face token.

| Package | Backend | Model directories |
| --- | --- | --- |
| x86_64 and Arm | ONNX | `cam-contact`, `gaze-detection`, `osnet_x0_25`, `ultraface`, `yolo26`, `yolov11` |
| x86_64 and Arm | ExecuTorch (experimental) | `yolox` |

## Run packaged inference

Model descriptors, binaries, and compatible OpChains are under `share/pek`.
They are local and require no runtime download. For example:

```python
import os
from pathlib import Path

import gi

gi.require_version("Gst", "1.0")
from gi.repository import Gst

package_root = Path(os.environ["PEK_PACKAGE_ROOT"])
os.environ["GST_PLUGIN_PATH"] = str(package_root / "lib/gstreamer-1.0")
Gst.init(None)

pipeline = Gst.parse_launch(
    "videotestsrc num-buffers=30 ! videoconvert ! "
    "video/x-raw,format=BGRA ! pekinfer name=infer ! fakesink"
)
pipeline.get_by_name("infer").set_property(
    "opchain-path", str(package_root / "share/pek/models/yolov11/opchain.json")
)
pipeline.set_state(Gst.State.PLAYING)
message = pipeline.get_bus().timed_pop_filtered(
    Gst.CLOCK_TIME_NONE, Gst.MessageType.ERROR | Gst.MessageType.EOS
)
pipeline.set_state(Gst.State.NULL)
if message.type == Gst.MessageType.ERROR:
    raise RuntimeError(message.parse_error()[0].message)
```

`peksink` serves the packaged browser UI from `web/content` by default. Its
`static-files` property remains available when an application needs an explicit
web root. Top-level `pek-menu` pipeline presets and sample media are
intentionally not part of the binary release.

The same smoke path is run natively for x86_64 and Arm packages on pull
requests targeting `main`. Pushes to `main` publish the three matching archives
on one GitHub Release and together in Artifactory under `releases/<version>/`,
the Perception wheel to Artifactory PyPI, and the matching multi-architecture
image in GHCR. Manual runs publish the archives and wheel only to Artifactory
under `snapshots/<label>/<full-sha>-<run-id>-<attempt>/`. The generic archive
and snapshot paths use
`https://artifactory.arm.com/artifactory/ai-expkits-internal.opk-ci` as their
base URL. The final Artifactory workflow log and job summary contain the folder,
links, hashes, and the matching run-unique GHCR snapshot reference.

[Back to Getting Started](/getting-started)
