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

Verify the hash shown beside the archive on the GitHub Release or Artifactory
workflow summary before extracting it:

```bash
sha256sum pek-<version>-linux-<architecture>.tar.gz
```

Architecture packages contain the six PEK plugins, the private
`lib/pek/pek-runtime.so` and common libraries, compatible model binaries and
OpChains, JSON schemas, `peksink` web assets, approved notices, and ONNX
Runtime.

They deliberately exclude `pek-menu`, pipeline presets, examples, sample
media, documentation, source, tests, debug files, NCNN, public C++ headers,
unused ONNX provider libraries, Hailo models and operation modules, and
accelerator drivers or firmware.

## Host prerequisites

PEK packages target Debian Trixie. Install the system GStreamer runtime and
tools with the Base, Good, and Bad plugin sets, including Nice and the WebRTC
plugins. GLib, Cairo, OpenSSL, zlib, Brotli, zstd, libsoup 3, json-glib, the
C/C++ runtimes, and any required accelerator driver and firmware remain host
dependencies. The Arm package also requires the system `libusb-1.0` runtime.
Python applications also need the system PyGObject GStreamer bindings,
available as `python3-gst-1.0` on Debian Trixie.

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
on one GitHub Release and together in Artifactory under `releases/<version>/`.
Manual runs publish them only to Artifactory under
`snapshots/<label>/<full-sha>-<run-id>-<attempt>/`. Both paths use
`https://artifactory.arm.com/artifactory/ai-expkits-internal.opk-ci` as their
base URL. The final Artifactory workflow log and job summary contain the folder,
links, and hashes.

[Back to Getting Started](/getting-started)
