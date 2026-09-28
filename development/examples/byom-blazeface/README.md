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

# Bring Your Own Model: BlazeFace

This example shows how to run an external ONNX model with the OPK
Kit without adding model-specific C++ code or generating a new SDK type.

It uses the Apache-2.0 BlazeFace model from
`fernandotonon/QtMeshEditor-blazeface-onnx`, pinned to revision
`50f2c66ffbdf84beae8c267df2b49e5c5a5162e9`. The model descriptor records that
source through `hfDownload`. On the first run, the example asks the repository
model provisioning tool to download and verify the file before OPK loads it.

```text
model.json
    -> opchain.json
        -> OPK preprocessing and ONNX inference
        -> postprocess.py
            -> caller-owned JSON in FrameResults
                -> opkcomm NDJSON
                    -> Open Perception Kit SDK consumer
```

OPK owns preprocessing, inference execution, and result transport. The model
integration owns the tensor contract and postprocessing. The application owns
the custom result schema and its meaning.

## What is part of the integration?

The files are separated so the reusable BYOM contract stays visible:

- `model.json`, `opchain.json`, and `postprocess.py` are the model integration.
  They describe the tensor contract, compose standard OPK operations, and turn
  model outputs into application-owned results.
- `pipeline.json` is the minimal runnable media flow used to exercise that
  integration through `opkinfer` and transport results through `opkcomm`.
- `support/results.py` is the standalone application-side consumer. It shows
  how a user retrieves and validates the external payload with the Open
  Perception Kit SDK.
- `run.py` and the remaining `support/` modules are demonstration support.
  They provision the model, manage processes and shutdown, follow NDJSON, and
  optionally render a verification video. Applications normally replace this
  orchestration with their own lifecycle and presentation code.

## 1. Describe the model

The three configuration JSON files use `"version": "1.0.0"`. Increment an
edited file's patch within its current major/minor contract. A major mismatch
fails, a minor mismatch warns and continues, and patch differences are ignored
at runtime; see [Configuration compatibility](../../../docs/public/concepts/configuration-compatibility.md).

`model.json` defines the contract between BlazeFace and OPK:

- local model file
- input and output tensor shapes and types
- image layout and color format
- preprocessing normalization

The `hfDownload` entry records the repository, revision, filename, and expected
digest. The example runner materializes that declaration through the standard
repository model provisioning tool. OPK inference itself still consumes only
the local path in `modelFile`.

BlazeFace expects a float32 RGB NHWC tensor with shape `[1, 128, 128, 3]`.
OPK resizes the frame and applies mean `0.5` and standard deviation `0.5`,
mapping values from `[0, 1]` to `[-1, 1]`.

This example uses direct resize rather than letterboxing, so normalized output
coordinates apply directly to the complete source frame.

## 2. Build the OpChain

`opchain.json` composes reusable framework operations with model-specific
postprocessing:

```text
InferenceController
  -> GenericImagePreprocess
  -> ONNX Inference
  -> PythonScript
```

The standard operations prepare the frame and execute the ONNX graph. The final
`PythonScript` operation calls `postprocess.py` with the output tensors and the
current `FrameResults` envelope.

This is the primary BYOM extension point: OPK remains model-independent while
the model integration defines how raw outputs become useful results.

## 3. Interpret the model outputs

`postprocess.py` contains the BlazeFace-specific implementation. It validates
the tensor names, shapes, and types before decoding the 896 anchor candidates,
converting logits to confidence values, and applying weighted non-maximum
suppression.

The result is compact UTF-8 JSON stored under an external FrameResults key:

```text
com.arm.example.blazeface.faces.v1
```

```json
{"faces":[{"x":0.25,"y":0.18,"width":0.3,"height":0.42,"confidence":0.96}]}
```

OPK carries these bytes but does not interpret or validate the JSON. The
producer and consumer own the schema, validation, and versioning; the `.v1`
suffix identifies this contract version.

Use an external payload for application-specific or independently versioned
data. Use a generated FrameResults payload type when the result must become a shared,
framework-supported contract.

## 4. Run the media pipeline

`pipeline.json` connects the model to the prerecorded input:

```text
filesrc -> decodebin -> videoconvert -> BGRA
        -> opkinfer -> opkcomm -> fakesink
```

`opkinfer` loads the OpChain and executes it for each frame. `opkcomm`
serializes the complete FrameResults packet as base64 inside NDJSON. `fakesink`
keeps the pipeline independent of a display, server, tracker, or overlay.

## 5. Consume FrameResults

`run.py` starts the pipeline and follows the NDJSON output. The consumer in
`support/results.py`:

1. validates the `opkcomm` record and encoding
2. decodes the packet with `open_perception_kit.packet`
3. verifies the producer identity
4. retrieves bytes using the same external key
5. validates the caller-owned JSON
6. prints normalized face rectangles

The Open Perception Kit SDK owns safe packet access. The application owns the semantics
of its external payload.

## Run the example

Use an official OPK development or deployment environment with ONNX and Python
Ops enabled. The first run requires network access to Hugging Face; subsequent
runs reuse the verified local model. The prerecorded video under `data/videos/`
must be available.

```bash
cd development/examples/byom-blazeface
python3 run.py
```

Results are printed as frames are processed:

```text
frame 17: 2 faces
  face 1: x=0.213 y=0.167 width=0.184 height=0.301 confidence=0.982
  face 2: x=0.631 y=0.192 width=0.171 height=0.284 confidence=0.947
```

When `ffmpeg` and `ffprobe` are available, the runner also creates
`blazeface-detections.mp4` from the validated rectangles. FFmpeg is only used
for this verification artifact; it is not part of inference or postprocessing.

Ctrl-C requests graceful shutdown and removes temporary and partial files.

## Adapt the pattern

To integrate another ONNX model:

1. Describe its exact tensor and preprocessing contract in `model.json`.
2. Reference that descriptor from the inference operation in `opchain.json`.
3. Implement model-specific tensor validation and decoding in `postprocess.py`.
4. Choose a generated FrameResults payload type or a caller-owned external payload.
5. Consume and validate the FrameResults packet with a matching Open Perception Kit SDK.
6. Test the complete flow with representative input data.

The BlazeFace decoder, anchors, thresholds, and JSON schema are model-specific.
The OPK preprocessing, inference, FrameResults, and transport surfaces are
reusable.

## Limitations

This example does not define a supported production payload API. It includes no
generated detection type, landmarks, tracking, OPK overlay, HTTP service,
WebUI, or real-time display path.
