#!/usr/bin/env python3
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

"""Opt-in native CREPE smoke test; requires the downloaded tiny.onnx and a build.

Run in the OPK container with numpy, flatbuffers and GStreamer Python bindings.
Checks the model-local chain and serialized output, without downloading artifacts.
"""
import base64
import json
import math
import os
from pathlib import Path
import sys
import tempfile
import time

import gi
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "generated/open_perception_kit/python/src"))
from open_perception_kit.sdk import Envelope  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.PitchEstimates import PitchEstimatesT  # noqa: E402


def main():
    model_dir = ROOT / "config/models/crepe-tiny"
    assert (model_dir / "tiny.onnx").is_file(), "Download crepe-tiny with scripts/download-models.py first"
    os.environ["GST_PLUGIN_PATH"] = str((ROOT / "development/build-active/meson-out").resolve())
    gi.require_version("Gst", "1.0")
    from gi.repository import Gst
    Gst.init(None)
    times = np.arange(1024) / 16000
    tone = np.sin(2 * np.pi * 440 * times)
    for name, signal, voiced in [("silence", np.zeros(1024), False),
                                 ("tone", .1 * tone, True),
                                 ("quiet-tone", .0001 * tone, False)]:
        with tempfile.TemporaryDirectory(prefix="crepe-integration-") as temporary:
            source = Path(temporary) / "audio.f32"
            target = Path(temporary) / "results.jsonl"
            signal.astype("<f4").tofile(source)
            pipeline = Gst.parse_launch(
                f"filesrc location={source} blocksize=4096 ! "
                "audio/x-raw,format=F32LE,rate=16000,channels=1,layout=interleaved ! "
                "opkinfer active=true audio-hop-samples=160 "
                f"opchain-path={model_dir / 'opchain.json'} ! "
                f"opkcomm method=file file-name={target} ! fakesink sync=false")
            try:
                pipeline.set_state(Gst.State.PLAYING)
                message = pipeline.get_bus().timed_pop_filtered(
                    60 * Gst.SECOND, Gst.MessageType.ERROR | Gst.MessageType.EOS)
                assert message is not None, "Pipeline timed out"
                assert message.type == Gst.MessageType.EOS, str(message.parse_error())
                deadline = time.monotonic() + 10
                while True:
                    contents = target.read_text() if target.exists() else ""
                    if contents.endswith("\n"):
                        break
                    assert time.monotonic() < deadline, "Timed out waiting for published packet"
                    time.sleep(.01)
            finally:
                pipeline.set_state(Gst.State.NULL)
            estimates = []
            for line in contents.splitlines():
                packet = json.loads(line)["frame_results_packet_b64"]
                if packet is None:
                    continue
                envelope = Envelope(base64.b64decode(packet))
                assert envelope.valid(), envelope.error()
                for payload in envelope.for_each(PitchEstimatesT):
                    estimates.extend(payload.estimates)
            assert len(estimates) == 1, (name, len(estimates))
            estimate = estimates[0]
            assert estimate.voiced == voiced, (name, estimate.voiced, estimate.periodicity)
            assert estimate.windowStartSample == 0 and estimate.windowEndSample == 1024
            assert 0 <= estimate.periodicity <= 1
            if voiced:
                assert abs(1200 * math.log2(estimate.frequencyHz / 440)) < 25, estimate.frequencyHz
            else:
                assert estimate.frequencyHz is None
            print(f"{name}: voiced={estimate.voiced}, Hz={estimate.frequencyHz}, periodicity={estimate.periodicity}")


if __name__ == "__main__":
    main()
