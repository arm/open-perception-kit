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


# YAMNet audio events

This integration uses the Qualcomm YAMNet v0.66.0 ONNX export with Float32 input
`audio: [1,1,96,64]` and logits `class_scores: [1,521]`. Keep `yamnet.onnx` and
its external weights `yamnet.data` together beside `model.json`.

Run the repository artifact downloader from the project root:

```sh
python scripts/download-models.py --models-dir config/models
```

The downloader provisions the configured models, including YAMNet's graph, weights,
`metadata.json` and `labels.txt`. `config/model-assets.json` pins the Qualcomm ZIP
linked from [the Hugging Face model card](https://huggingface.co/qualcomm/YamNet/tree/2105a5e3e546b80c484a7084fcae435683a1c205),
with SHA-256 checksums for both the archive and each selected file. v0.66.0 was
the latest published export on 2026-11-17. The Hugging Face repository contains
release information; the actual model archive is hosted by Qualcomm.
The container's `opk-models` stage uses the same downloader. Downloaded artifacts
are excluded from Git and the Docker build context; only their manifest is checked in.
Model notices are downloaded through `config/model-notices.json`.

The chain matches the frontend in `w-hc/torch_audioset` revision `e8852c5` used
by Qualcomm's wrapper: mono 16 kHz PCM, 512-point FFT, a periodic 400-sample Hann,
160-sample frame hop, magnitude HTK mel bands from 125 to 7500 Hz, and
`ln(mel + 0.001)` without normalization. Each 15680-sample window is centered
with reflection padding and cropped to its first 96 feature frames. Reflection
is repeated locally for each scheduled window, as in independently chunked
reference inference; this is not a continuous-stream STFT.

The supplied ONNX graph ends in a linear classifier. `AudioEventClassificationParser`
applies sigmoid independently, retains up to five events at confidence >= 0.5,
and emits categorical `Classifications` with label set `audioset:yamnet-521`,
class ID, label and probability. When no events qualify, it still publishes a
typed classification with the same label set and an empty candidate list; absence
of a payload means no assessment was published. Model and OpChain descriptors use versions
`2.0.0` and `2.1.0`, respectively.
Temporary debug logging is enabled with `log: true` in `opchain.json`. It prints
each accepted event directly to stderr on every inference window, independently
of the launcher’s log level; set it to `false`
to silence these messages.

The labels in `opchain.json` follow AudioSet class order. Tune `topK` and
`confidenceThreshold` in the chain; do not apply softmax to these logits.

After building in the OPK container, use `opk-menu` to select `yamnet-esc`.
The preset receives browser microphone audio through `opksrc` (signaling port
8003), runs every 1600 new samples (100 ms), and publishes classification results
through `opkcomm` at WebSocket endpoint `/ws`. Video from `opksrc` and processed
audio are sent to `opksink` for WebRTC playback. Enable microphone capture in
the web frontend.
Validate the preset without starting capture with:

```sh
./tools/opk-menu -p yamnet-esc
```

For deterministic smoke tests, replace the `source.src` audio branch input with
a finite `audiotestsrc` or decoded WAV source. The scheduler waits for a full window and
discards partial windows at EOS. Model labels are frame-level assessments, not
event onset/duration estimates; classification payloads have no audio-window bounds.

Run the optional artifact parity test in the development container using a Python
environment with NumPy, Torch, torchaudio, ONNX Runtime, FlatBuffers and the container’s
GStreamer Python bindings (PyGObject) available:

```sh
python development/tests/yamnet_integration_test.py
```

It feeds deterministic silence, tone, noise and chirp PCM through the native
GStreamer chain, decodes its published FlatBuffers classifications, and compares
class IDs, labels and probabilities against the reference frontend and ONNX Runtime.
Native frontend unit tests use checked-in golden tensors and do not require Torch.
