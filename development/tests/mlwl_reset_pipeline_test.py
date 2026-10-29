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

"""Opt-in: LD_PRELOAD=/tmp/v3d-reset-ioctl.so python mlwl_reset_pipeline_test.py BUILD MODEL_JSON OUTPUT."""
from contextlib import contextmanager
import ctypes
import json
from pathlib import Path
import sys
from pipeline_test_utils import load_gstreamer_plugins, release_pipeline


def check(condition, message):
    if not condition:
        raise AssertionError(message)


build, model, output = map(lambda p: Path(p).resolve(), sys.argv[1:4])
output.mkdir(parents=True, exist_ok=True)
Gst = load_gstreamer_plugins([build / "libopkinfer.so"])
check(Path(Gst.ElementFactory.find("opkinfer").get_plugin().get_filename()) == build / "libopkinfer.so",
      'incorrect opkinfer plugin loaded')
inject = ctypes.CDLL(None)
inject.reset_arm.argtypes = [ctypes.c_int, ctypes.c_int]
observe = output / "observe.py"
observe.write_text('''import hashlib, json, numpy
from pathlib import Path
from open_perception_kit.guest import Envelope
from opk_python_ops import Context, Tensor, python_script
@python_script
def process(env: Envelope, tensors: tuple[Tensor, ...], context: Context) -> None:
    if not tensors or not all(numpy.isfinite(t.array).all() for t in tensors):
        raise RuntimeError('missing or non-finite MLWL output tensors')
    record = [hashlib.sha256(t.array.tobytes()).hexdigest() for t in tensors]
    with Path(__file__).with_suffix('.jsonl').open('a') as log:
        log.write(json.dumps(record) + '\\n')
''')
chain = output / "opchain.json"
chain.write_text(json.dumps({
    "version": "1.5.0",
    "execution": {"preprocessing": "gpu"},
    "name": "reset-integration",
    "description": "Real MLWL reset detection with raw tensor observation",
    "ops": [
        {"id": "opk-std-ops/InferenceController", "attributes": {}},
        {"id": "opk-std-ops/GenericImagePreprocess", "attributes": {}},
        {"id": "opk-mlwl-ops/Inference",
            "attributes": {"modelDescriptor": str(model)}},
        {"id": "opk-python-ops/PythonScript",
            "attributes": {"script": str(observe)}},
    ],
}))


@contextmanager
def pipeline():
    observe.with_suffix('.jsonl').write_text('')
    instance = Gst.parse_launch(
        'appsrc name=src format=time caps="video/x-raw,format=BGRA,width=320,height=240,framerate=1/1" ! '
        f'opkinfer name=infer opchain-path="{chain}" qos=false ! appsink name=sink sync=false async=false')
    try:
        check(instance.set_state(Gst.State.PLAYING) !=
              Gst.StateChangeReturn.FAILURE, 'startup failed')
        yield instance
    finally:
        release_pipeline(instance, Gst)


def push(instance, frame, value):
    data = bytes((value, value, value, 255)) * (320 * 240)
    buffer = Gst.Buffer.new_allocate(None, len(data), None)
    buffer.fill(0, data)
    buffer.pts, buffer.duration = frame * Gst.SECOND, Gst.SECOND
    check(instance.get_by_name('src').emit('push-buffer', buffer)
          == Gst.FlowReturn.OK, 'push failed')


def receive(instance, frame):
    sample = instance.get_by_name('sink').emit(
        'try-pull-sample', 60 * Gst.SECOND)
    check(sample and sample.get_buffer().pts ==
          frame * Gst.SECOND, f'expected frame {frame}')


def finish(instance):
    instance.get_by_name('src').emit('end-of-stream')
    message = instance.get_bus().timed_pop_filtered(
        60 * Gst.SECOND, Gst.MessageType.ERROR | Gst.MessageType.EOS)
    check(message and message.type == Gst.MessageType.EOS, 'expected EOS')
    check(instance.get_by_name('sink').emit(
        'try-pull-sample', 0) is None, 'extra frame escaped')


def expect_error(instance, text):
    message = instance.get_bus().timed_pop_filtered(
        60 * Gst.SECOND, Gst.MessageType.ERROR)
    check(message and message.src.get_name() ==
          'infer', 'expected opkinfer error')
    check(text in str(message.parse_error()), str(message.parse_error()))
    check(instance.get_by_name('sink').emit(
        'try-pull-sample', 0) is None, 'failed frame escaped')


def tensors():
    return [json.loads(line) for line in observe.with_suffix('.jsonl').read_text().splitlines()]


with pipeline() as instance:
    for frame, value in enumerate((255, 0, 255)):
        push(instance, frame, value)
        receive(instance, frame)
    finish(instance)
reference = tensors()
check(len(reference) == 3 and reference[0] == reference[2] and reference[0] != reference[1],
      'input-sensitive reference required')

for delay in (0, 1):  # Detect a reset before or after GPU execution.
    with pipeline() as instance:
        push(instance, 0, 255)
        receive(instance, 0)
        inject.reset_arm(1, delay)
        push(instance, 1, 0)
        expect_error(instance, 'V3D GPU reset')
    check(tensors() == [reference[0]], 'reset frame published tensors')

with pipeline() as instance:
    push(instance, 0, 255)
    receive(instance, 0)
    finish(instance)
check(tensors() == [reference[0]], 'fresh pipeline differs from reference')

with pipeline() as instance:
    inject.reset_arm(2, 1)  # Fail the post-execution counter query.
    push(instance, 0, 255)
    expect_error(instance, 'Reading V3D global reset counter')
check(not tensors(), 'counter-query failure published tensors')

print('PASS: normal inference, reset errors, explicit restart, counter-query failure')
