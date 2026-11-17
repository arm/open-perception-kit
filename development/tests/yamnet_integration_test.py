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

"""Opt-in artifact test: native YAMNet pipeline versus Torch frontend and ORT.

Run in the OPK container after building, with numpy, torch, torchaudio,
onnxruntime, flatbuffers and GStreamer Python bindings available. Requires local yamnet.onnx/yamnet.data.
The reference follows torch_audioset e8852c5's frontend, including sqrt of
FFT power, centered reflection and cropping to the first 96 frames.
"""
import base64
import json
import os
from pathlib import Path
import sys
import tempfile
import time

import gi

import numpy as np
import onnxruntime as ort
import torch
import torchaudio

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'generated/open_perception_kit/python/src'))
from open_perception_kit.sdk import Envelope  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.Classifications import ClassificationsT  # noqa: E402


def main():
    torch.set_num_threads(1)
    model_dir = ROOT / 'config/models/yamnet-esc'
    chain = json.loads((model_dir / 'opchain.json').read_text())
    frontend = chain['ops'][0]['attributes']['logMel']
    post = chain['ops'][-1]['attributes']
    options = ort.SessionOptions()
    options.intra_op_num_threads = 1
    session = ort.InferenceSession(str(model_dir / 'yamnet.onnx'), options,
                                   providers=['CPUExecutionProvider'])
    spectrum = torchaudio.transforms.Spectrogram(
        n_fft=512, win_length=400, hop_length=160, power=2,
        center=True, pad_mode='reflect')
    mel = torchaudio.transforms.MelScale(
        n_mels=64, sample_rate=16000, f_min=125, f_max=7500, n_stft=257,
        norm=None, mel_scale='htk')
    count = frontend['windowSampleCount']
    sample_times = np.arange(count) / 16000
    samples = {
        'silence': np.zeros(count),
        'tone': .1 * np.sin(2 * np.pi * 440 * sample_times),
        'noise': np.random.default_rng(42).normal(0, .03, count),
        'chirp': .1 * np.sin(2 * np.pi * (100 * sample_times + 3000 * sample_times * sample_times)),
    }
    plugin_dir = (ROOT / 'development/build-active/meson-out').resolve()
    os.environ['GST_PLUGIN_PATH'] = str(plugin_dir)
    gi.require_version('Gst', '1.0')
    from gi.repository import Gst
    Gst.init(None)
    for name, signal in samples.items():
        pcm = signal.astype('<f4')
        features = torch.log(mel(spectrum(torch.from_numpy(pcm)).sqrt()) + .001)
        features = features.T[:96].contiguous()[None, None].numpy()
        logits = session.run(None, {'audio': features})[0][0]
        probabilities = torch.sigmoid(torch.from_numpy(logits)).numpy()
        expected = sorted((i for i, p in enumerate(probabilities)
                           if p >= post['confidenceThreshold'] and post['labels'][i] is not None),
                          key=lambda i: (-float(probabilities[i]), i))[:post['topK']]
        with tempfile.TemporaryDirectory(prefix='yamnet-integration-') as temporary:
            source = Path(temporary) / 'audio.f32'
            target = Path(temporary) / 'results.jsonl'
            pcm.tofile(source)
            command = ['filesrc', f'location={source}',
                       f'blocksize={pcm.nbytes}', '!',
                       'audio/x-raw,format=F32LE,rate=16000,channels=1,layout=interleaved', '!',
                       'opkinfer', 'active=true', 'audio-hop-samples=1600',
                       f'opchain-path={model_dir / "opchain.json"}', '!',
                       'opkcomm', 'method=file', f'file-name={target}', '!', 'fakesink', 'sync=false']
            pipeline = Gst.parse_launch(' '.join(command))
            try:
                pipeline.set_state(Gst.State.PLAYING)
                message = pipeline.get_bus().timed_pop_filtered(
                    60 * Gst.SECOND, Gst.MessageType.ERROR | Gst.MessageType.EOS)
                assert message is not None, 'Pipeline timed out'
                assert message.type == Gst.MessageType.EOS, str(message.parse_error())
                # opkcomm publishes asynchronously and drops queued work on shutdown.
                # Keep the pipeline alive until the single input buffer's JSON line is written.
                deadline = time.monotonic() + 10
                while True:
                    contents = target.read_text() if target.exists() else ''
                    if contents.endswith('\n'):
                        break
                    assert time.monotonic() < deadline, 'Timed out waiting for published packet'
                    time.sleep(.01)
            finally:
                pipeline.set_state(Gst.State.NULL)
            payloads = []
            for line in contents.splitlines():
                packet = json.loads(line)['frame_results_packet_b64']
                if packet is None:
                    continue
                envelope = Envelope(base64.b64decode(packet))
                assert envelope.valid(), envelope.error()
                payloads.extend(envelope.for_each(ClassificationsT))
            assert len(payloads) == (1 if expected else 0), (name, len(payloads))
            if not expected:
                print(f'{name}: PASS (no qualifying events)')
                continue
            payload = payloads[0]
            assert payload.layer.contentType == b'esc'
            assert payload.layer.model == b'yamnet-esc'
            assert payload.layer.producer.implementation == b'AudioEventClassificationParser'
            candidates = payload.classifications[0].candidates
            assert [c.classId for c in candidates] == expected, (name, expected, candidates)
            for candidate, index in zip(candidates, expected):
                assert candidate.text.decode('utf-8') == post['labels'][index]
                np.testing.assert_allclose(candidate.confidence, probabilities[index], atol=2e-4, rtol=0)
            print(f"{name}: PASS ({candidates[0].text.decode('utf-8')}, {candidates[0].confidence:.6f})")


if __name__ == '__main__':
    main()
