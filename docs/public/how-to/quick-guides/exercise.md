---
sidebar_position: 9
sidebar_label: Exercise quick guide
---

# Exercise Quick Guide

This guide is a short hands-on exercise for understanding how the repository fits together.

Instead of starting from the most advanced checked-in demo immediately, this exercise builds a pipeline step by step and shows which files in the codebase are responsible for each layer.

The goal is not to invent a new architecture. The goal is to reuse the checked-in codebase and gradually understand:

- how a top-level pipeline preset is written
- how sources and sinks are swapped
- how `ampinfer` is inserted
- how the camera-contact path depends on UltraFace first
- how `amposd` and `ampperformance` fit into the pipeline
- where the model descriptor, opchain, postprocessing, and visualization logic live

## Before you start

This guide assumes that:

- the repository is already cloned
- the project builds successfully
- `tools/amp-menu` is available

If you only want the shortest setup path first, use one of the platform quick guides and then come back here.

- [Windows/Linux Quick Guide](win-lin.md)
- [Raspberry Pi Quick Guide](rpi.md)
- [macOS Quick Guide](mac.md)

If you want to use your own media during the exercise, use:

```text
data/images/
data/videos/
```

Inside the container, those locations are typically available under:

```text
/work/data/images/
/work/data/videos/
```

## Step 1: Create a new top-level pipeline preset

Create a new JSON file under:

```text
config/pipelines/
```

For example:

```text
config/pipelines/exercise-camera-contact.json
```

In this repository, this top-level pipeline JSON is the format that `amp-menu` reads.

Start with the smallest valid preset shape:

```json
{
  "description": "Exercise pipeline built step by step.",
  "pipeline": [
    "filesrc location=/work/data/videos/example.mp4 !",
    "decodebin !",
    "videoconvert ! video/x-raw,format=BGRA !",
    "fakesink"
  ]
}
```

This is the first important repository concept:

- `config/pipelines/` holds top-level runnable presets
- the `pipeline` array is concatenated into the final GStreamer pipeline string
- you can evolve the preset incrementally without touching runtime source code yet
- it is often easiest to start with a simple video source first, then switch to a repeated image source when you want a more controlled and easy-to-recognize result

> Expected result: the new preset appears in `amp-menu` and runs a valid video pipeline that ends in `fakesink`.

## Step 2: Use an image source and `fakesink`

Now switch from a video file to a single repeated image while keeping `fakesink` at the end.

The smallest useful image-based pipeline for this exercise is:

```json
{
  "description": "Image source to fakesink.",
  "pipeline": [
    "filesrc location=/work/data/images/katana.jpg !",
    "jpegdec !",
    "imagefreeze !",
    "videoconvert ! video/x-raw,format=BGRA !",
    "fakesink"
  ]
}
```

This is useful because it removes most source-side complexity from the exercise. From this point onward, the next steps can focus on AMP elements rather than on video timing or container decoding.

At this point, you are only proving that:

- the source image can be loaded
- the image is decoded
- the frame is converted into the BGRA format expected by the current video-processing elements

> Expected result: the pipeline runs successfully, but no visible output is produced yet.

## Step 3: Switch to an image source and `filesink`

Now replace `fakesink` with a file-writing sink path.

A minimal example is:

```json
{
  "description": "Image source to filesink.",
  "pipeline": [
    "filesrc location=/work/data/images/katana.jpg !",
    "jpegdec !",
    "imagefreeze !",
    "videoconvert ! video/x-raw,format=BGRA !",
    "videoconvert !",
    "jpegenc !",
    "filesink location=/work/data/output/exercise-frame.jpg"
  ]
}
```

This step proves that you can swap only the sink side and produce a concrete artifact without changing the source side.

> Expected result: the pipeline writes a frame to `/work/data/output/exercise-frame.jpg`.

## Step 4: Switch to an image source and `ampsink`

Now change the output side to the checked-in browser-facing sink.

```json
{
  "description": "Image source to ampsink.",
  "pipeline": [
    "filesrc location=/work/data/images/katana.jpg !",
    "jpegdec !",
    "imagefreeze !",
    "videoconvert ! video/x-raw,format=BGRA !",
    "ampsink name=sink"
  ]
}
```

This is the first point where the pipeline becomes visible through the current AMP UI stack.

> Expected result: the pipeline runs and the image is reachable through the current `ampsink`-hosted web UI.

## Step 5: Include inference for camera contact

Now insert `ampinfer`.

For this exercise, use the existing checked-in opchain:

```text
/work/config/opchains/cam-contact/opchain.json
```

That opchain already includes both stages needed for the camera-contact flow:

1. UltraFace face detection
2. camera-contact classification on the detected face crops

A first inference-enabled version looks like this:

```json
{
  "description": "Image source with camera-contact inference.",
  "pipeline": [
    "filesrc location=/work/data/images/katana.jpg !",
    "jpegdec !",
    "imagefreeze !",
    "videoconvert ! video/x-raw,format=BGRA !",
    "ampinfer opchain-path=/work/config/opchains/cam-contact/opchain.json active=false !",
    "ampsink name=sink"
  ]
}
```

This is an important repository concept: the top-level pipeline does not need to describe every model stage directly. It can delegate the inference logic to an OpChain.

> Expected result: the pipeline still runs through `ampsink`, but now the buffer also carries `PerceptionMeta` produced by the camera-contact inference chain.

## Step 6: Include the OSD element

Now add `amposd` so the structured results can be drawn onto the frame.

```json
{
  "description": "Image source with camera-contact inference and OSD.",
  "pipeline": [
    "filesrc location=/work/data/images/katana.jpg !",
    "jpegdec !",
    "imagefreeze !",
    "videoconvert ! video/x-raw,format=BGRA !",
    "ampinfer opchain-path=/work/config/opchains/cam-contact/opchain.json active=false !",
    "amposd enabled=true !",
    "ampsink name=sink"
  ]
}
```

At this point, the data path becomes easier to understand:

- `ampinfer` writes structured results into `PerceptionMeta`
- `amposd` reads `PerceptionMeta`
- `amposd` draws the supported overlay elements onto the BGRA frame

> Expected result: face detections and camera-contact indicators become visible on the output frame.

## Step 7: Include performance measurement

Now add `ampperformance` before `amposd`.

```json
{
  "description": "Image source with inference, performance, OSD, and ampsink.",
  "pipeline": [
    "filesrc location=/work/data/images/katana.jpg !",
    "jpegdec !",
    "imagefreeze !",
    "videoconvert ! video/x-raw,format=BGRA !",
    "ampinfer opchain-path=/work/config/opchains/cam-contact/opchain.json active=false !",
    "ampperformance show-all-metrics=true x-offset=20 y-offset=20 font-size=18 alpha=0.9 update-interval=1 !",
    "amposd enabled=true !",
    "ampsink name=sink"
  ]
}
```

This is now very close to the checked-in camera-contact preset.

The fully checked-in example to compare against is:

```text
config/pipelines/cam-connect.json
```

> Expected result: the output shows both the normal overlay and the formatted performance overlay text.

## Step 8: For a deeper understanding, inspect the codebase pieces behind the exercise

> The most important idea here is that the top-level pipeline is only the outer shell. This section is for readers who want to understand how the checked-in example is assembled and how a similar integration would be done for their own model. For more detail after reading through this quick exercise, continue with [Bring your model](../deep-dives/bring-your-model.md) and [Custom postprocessing](../deep-dives/custom-postprocessing.md).

The top-level pipeline is only the outer shell. The real camera-contact behavior is split across these files:

- model descriptor
- opchain
- postprocessing parser
- OSD visualization logic

### Step 8.1: Camera-contact model descriptor

Path:

```text
config/models/cam-contact/model.json
```

Relevant snippet:

```json
{
  "name": "cam_contact",
  "modelFamily": "cam_contact",
  "modelFile": "cam_contact_mobilenetv2_100.ra_in1k-a8w8-ort-quantized.onnx",
  "dynamicOutput": false,
  "contentType": "cameraContact",
  "inputTensors": [
    {
      "shape": [1, 3, 224, 224],
      "dataKind": "ImageRgbChw",
      "valueType": "Float32"
    }
  ],
  "outputTensors": [
    {
      "shape": [1, 2],
      "dataKind": "RawTensorData",
      "valueType": "Float32"
    }
  ]
}
```

This file tells the inference Op what model is being loaded and what tensor contract is expected.

### Step 8.2: Camera-contact opchain

Path:

```text
config/opchains/cam-contact/opchain.json
```

Relevant snippet:

```json
{
  "ops": [
    {
      "id": "amp-std-ops/InferenceController",
      "attributes": {}
    },
    {
      "id": "amp-std-ops/GenericImagePreprocess",
      "attributes": {
        "inputImageTensorIndex": 0,
        "inputImageSourceName": "pipelineVideoFrame"
      }
    },
    {
      "id": "amp-onnx-ops/Inference",
      "attributes": {
        "modelDescriptor": "/work/config/models/ultraface/ultraface.json"
      }
    },
    {
      "id": "amp-std-ops/GenericPostprocess",
      "attributes": {
        "parser": "UltrafaceParser"
      }
    },
    {
      "id": "amp-std-ops/InferenceController",
      "loopId": 2,
      "attributes": {
        "contentType": "humanFace"
      }
    },
    {
      "id": "amp-onnx-ops/Inference",
      "loopId": 1,
      "attributes": {
        "modelDescriptor": "/work/config/models/cam-contact/model.json"
      }
    },
    {
      "id": "amp-std-ops/GenericPostprocess",
      "loopId": 1,
      "attributes": {
        "parser": "CameraContactParser",
        "contactClassIndex": 1,
        "noContactClassIndex": 0
      }
    }
  ]
}
```

This is where the two-stage logic lives. The top-level pipeline only sees one `ampinfer`, but the opchain contains both the face detector and the camera-contact classifier.

### Step 8.3: Camera-contact postprocessing element

Path:

```text
development/ops-std/postproc/CameraContactParser.cpp
```

Relevant snippet:

```cpp
amp::Result<void> CameraContactParser::parse(const amp::TensorParser::Input &input,
                                             amp::Perception::Layer &detectionResult) {
    const auto &tensor = *input.tensors[0];
    const auto shape = tensor.getShape();

    if (shape.dimensionCount != 2 || shape.valueCount[0] != 1 || shape.valueCount[1] != 2) {
        return tl::unexpected(AMP_ERROR(
            amp::ErrorFlag::InvalidData,
            fmt::format("CameraContactParser expects [1,2] logits, got {}", shape.toString())));
    }

    const auto probabilities = softmax2(tensor);
    const bool isContact = probabilities[1] >= probabilities[0];

    amp::Perception::Classification classification;
    amp::Perception::Classification::Candidate candidate;
    candidate.classId = isContact ? 1 : 0;
    candidate.confidence = isContact ? probabilities[1] : probabilities[0];
    candidate.text = isContact ? "contact" : "no contact";

    classification.candidates.push_back(candidate);

    detectionResult.contentType = "cameraContact";
    detectionResult.detections.push_back(classification);

    return {};
}
```

This is where the raw `[1,2]` tensor becomes a structured `Perception::Classification` result in a `cameraContact` layer.

### Step 8.4: Camera-contact OSD visualization

Path:

```text
development/elements/amposd/amposd.cpp
```

Relevant snippet:

```cpp
static void drawCameraContactMarkers(Osd::Layer *layer, const amp::Perception &perception) {
    amp::ConstPerceptionTools perceptionTools(perception);

    for (const auto &inferLayer : perception.layers) {
        if (inferLayer.contentType != "cameraContact") {
            continue;
        }

        for (const auto &det : inferLayer.detections) {
            const auto *classification = std::get_if<amp::Perception::Classification>(&det);
            if (!classification || classification->candidates.empty()) {
                continue;
            }

            const auto &candidate = classification->candidates.front();
            std::vector<amp::Perception::Rect> parents =
                perceptionTools.getAllRectsWithContentType("humanFace", classification->parentUuid);

            if (parents.empty()) {
                continue;
            }

            const auto &face = parents.front();
            const bool hasCameraContact = candidate.classId == 1;
            const amp::Color markerColor = hasCameraContact ? amp::Colors::lime : amp::Colors::red;

            Osd::Circle::draw(*layer,
                              Osd::Coordinate{face.x + face.width * 0.5f,
                                              face.y + face.height * 0.5f},
                              std::min(face.width, face.height) * 0.5f,
                              markerColor,
                              5.0f);
        }
    }
}
```

This is where the `cameraContact` `Perception` result is turned into a visual overlay.

## Final comparison with the checked-in preset

Once you have gone through the steps above, compare your exercise pipeline with:

```text
config/pipelines/cam-connect.json
```

That file is the checked-in version of the same idea:

- image source
- camera-contact opchain
- performance overlay
- OSD
- `ampsink`

## What should you have at the end of this exercise?

By the end of this guide, you should understand:

- how top-level pipeline presets are defined under `config/pipelines/`
- how sources and sinks can be swapped independently of inference logic
- how one `ampinfer` element can hide a multi-stage opchain
- why camera contact depends on UltraFace first
- how postprocessing turns tensors into `Perception`
- how `amposd` turns `Perception` into a visible overlay

You should also be able to swap the example media path with your own file under `/work/data/images/` or `/work/data/videos/` and understand which layer of the repository you are changing when you edit the pipeline preset, the opchain, or the parser.

Success looks like this: you can read a checked-in pipeline, trace it into the model descriptor, opchain, parser, and visualization code, and make a small pipeline change without guessing.
