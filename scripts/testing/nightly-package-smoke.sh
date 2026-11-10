#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Runs inside a fresh Debian Trixie container with /artifacts and /source mounted read-only.
set -euo pipefail
shopt -s nullglob

if [[ $# -ne 1 ]] || [[ "$1" != archive && "$1" != debian ]]; then
    echo "Usage: $0 <archive|debian>" >&2
    exit 2
fi

if [[ "$1" == archive ]]; then
    case "$(dpkg --print-architecture)" in
        amd64) archive_arch=x86_64 ;;
        arm64) archive_arch=aarch64 ;;
        *)
            echo 'Unsupported Debian architecture' >&2
            exit 1
            ;;
    esac
    archives=(/artifacts/opk-*-linux-"$archive_arch".tar.gz)
    if [[ ${#archives[@]} -ne 1 ]]; then
        echo 'Expected one OPK archive' >&2
        exit 1
    fi
else
    runtime=(/artifacts/opk-runtime_*.deb)
    demo=(/artifacts/opk-demo_*.deb)
    [[ ${#runtime[@]} -eq 1 && ${#demo[@]} -eq 1 ]] || {
        echo 'Expected one runtime and one demo Debian package' >&2
        exit 1
    }
fi

export DEBIAN_FRONTEND=noninteractive

# Relies on set -e to stop when an inspection or pipeline fails.
smoke_runtime() {
    gst-inspect-1.0 opkinfer > /dev/null
    gst-inspect-1.0 opkperformance > /dev/null
    timeout 120s gst-launch-1.0 -q \
        videotestsrc num-buffers=2 ! video/x-raw,format=BGRA,width=320,height=240 ! \
        opkperformance ! fakesink
}

apt-get update
if [[ "$1" == debian ]]; then
    apt-get install -y --no-install-recommends "${runtime[0]}" "${demo[0]}" gstreamer1.0-tools
    dpkg-query -W -f='${db:Status-Status}\n' opk-runtime opk-demo | grep -x -c installed | grep -x 2
    smoke_runtime
    python3 /source/scripts/testing/nightly_image_pipeline.py \
        /usr/share/opk/config/pipelines/yolo26n-320.json /tmp/nightly-image-pipeline.json
    NO_COLOR=1 opk-menu --dry-run /tmp/nightly-image-pipeline.json | tee /tmp/opk-menu-smoke.log
    grep -q '^Model: OK ' /tmp/opk-menu-smoke.log
    grep -Fx 'Media: OK /usr/share/opk/data/images/GettyImages-1140581459-thumbnail.jpg' \
        /tmp/opk-menu-smoke.log
    if grep -q 'MISSING' /tmp/opk-menu-smoke.log; then
        echo 'Packaged pipeline has missing inputs' >&2
        exit 1
    fi
else
    apt-get install -y --no-install-recommends \
        ca-certificates gstreamer1.0-tools gstreamer1.0-nice \
        gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
        libfftw3-single3 libfmt10 libjson-glib-1.0-0 libsoup-3.0-0 libssl3t64 \
        libusb-1.0-0 libvulkan1 libpython3.13 python3 python3-venv zlib1g
    tar -C /tmp -xzf "${archives[0]}"
    package_root="/tmp/$(basename "${archives[0]}" .tar.gz)"
    unset LD_LIBRARY_PATH
    export GST_PLUGIN_PATH="$package_root/lib/gstreamer-1.0"
    export GST_REGISTRY=/tmp/opk-package-registry.bin
    smoke_runtime
    sdk_zips=("$package_root"/share/opk/open-perception-kit/open-perception-kit-*.zip)
    if [[ ${#sdk_zips[@]} -ne 1 ]]; then
        echo 'Expected one bundled SDK ZIP' >&2
        exit 1
    fi
    python3 -m zipfile -e "${sdk_zips[0]}" /tmp/opk-sdk
    sdk_wheels=(/tmp/opk-sdk/open-perception-kit-*/python/open_perception_kit-*.whl)
    if [[ ${#sdk_wheels[@]} -ne 1 ]]; then
        echo 'Expected one bundled SDK wheel' >&2
        exit 1
    fi
    /source/scripts/setup-python-ops-runtime.sh --python /usr/bin/python3 \
        --venv /tmp/opk-python \
        --runtime-json /source/development/ops-python/runtime.json \
        --sdk-json /source/tools/perception/sdk.json
    /tmp/opk-python/bin/pip install --no-cache-dir --no-deps "${sdk_wheels[0]}"
    export OPK_PYTHON_RUNTIME_VENV=/tmp/opk-python
    /source/scripts/release/smoke-opk-package.sh "${archives[0]}" \
        /source/development/tests/python_script_op/runtime_environment.py
fi
