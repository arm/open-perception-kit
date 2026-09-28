#!/usr/bin/env bash
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <archive.tar.gz> <python-operation.py>" >&2
    exit 2
fi

archive="$(realpath "$1")"
python_operation="$(realpath "$2")"
smoke_root="$(mktemp -d)"
trap 'rm -rf "$smoke_root"' EXIT
package_root="$smoke_root/$(basename "$archive" .tar.gz)"
tar -C "$smoke_root" -xzf "$archive"
test -d "$package_root"

export GST_PLUGIN_PATH="$package_root/lib/gstreamer-1.0"
export LD_LIBRARY_PATH="$package_root/lib/opk"
export GST_REGISTRY="$smoke_root/gstreamer-registry.bin"
for element in fakesink opusenc opkcomm opkinfer opkosd opkperformance \
    opksink opktracker videoconvert videotestsrc vp8enc webrtcbin; do
    gst-inspect-1.0 "$element" > /dev/null
done

cp "$python_operation" "$smoke_root/runtime_environment.py"
python3 -c \
    'import json, os, sys; json.dump({"version": "1.0.0", "name": "Python runtime smoke", "description": "Check the packaged Python runtime without inference.", "ops": [{"id": "opk-python-ops/PythonScript", "attributes": {"script": os.path.abspath(sys.argv[2])}}]}, open(sys.argv[1], "w", encoding="utf-8"))' \
    "$smoke_root/opchain-python-smoke.json" "$smoke_root/runtime_environment.py"
env -u OPK_DEVTOOLS_VENV -u OPK_PYTHON_RUNTIME_VENV \
    timeout 120s gst-launch-1.0 -q \
    videotestsrc pattern=ball num-buffers=1 ! \
    video/x-raw,format=BGRA,width=320,height=320,framerate=5/1 ! \
    opkinfer opchain-path="$smoke_root/opchain-python-smoke.json" ! \
    fakesink sync=false

timeout 120s gst-launch-1.0 -q \
    videotestsrc pattern=ball num-buffers=5 ! \
    video/x-raw,format=BGRA,width=320,height=320,framerate=5/1 ! \
    opksink
