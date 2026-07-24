---
title: Runtime Basics
sidebar_position: 2
sidebar_label: Runtime Basics
description: Understand how GStreamer pipelines, OpChains, model descriptors, and Perception results fit together at runtime.
---

# Runtime Basics

This page explains the practical runtime concepts you need when you want to run PEK with your own media or your own model files.

## What will you learn from this documentation?

If you follow this page successfully, you will learn how PEK uses pipelines, OpChains, and model descriptors together at runtime.

At the end of this page, you should be able to tell where to change the media source, where to change model execution behavior, and what a successful runtime path looks like from input to rendered result.

## GStreamer basics in PEK

PEK runs inside GStreamer pipelines.

A GStreamer pipeline is a chain of elements connected with `!`, for example:

```text
source ! convert ! process ! sink
```

In PEK, a typical video pipeline looks like this:

```text
source ! videoconvert ! pekinfer ! pekosd ! peksink
```

The source provides media, `pekinfer` runs AI processing, `pekosd` draws overlays, and `peksink` publishes the result.

## What is a pipeline?

In this repository, “pipeline” usually means the top-level GStreamer runtime assembled from a JSON preset under `config/pipelines/`.

Those presets define:
- the source, such as an image, video file, or camera
- the processing elements used in the stream
- the sink, usually `peksink`

The launcher `tools/pek-menu` reads these presets and runs them. For normal use, start it through the VS Code run tasks; for terminal use, call `./tools/pek-menu` from the active host side container or remote host container terminal at the project root.

If you want to change which image, video, or camera is used, this is usually the first place to edit.

Some checked-in presets intentionally set `pekinfer active=false`.
That lets the PEK web UI register the model first and then enable it from the **AI Models** panel when you are ready.

Model setup is asynchronous: while an activated model is being downloaded, verified, and loaded,
`pekinfer` remains pass-through so video delivery does not wait for setup. Inference execution after
the model becomes ready is still synchronous. A fully asynchronous inference execution flow is
planned for a later update, but it is not available yet.

## Logging does not make inference asynchronous

PEK logging uses a separate worker thread so normal logging calls do not perform output I/O on the
calling component's thread. This is independent of GStreamer pipeline scheduling and inference
execution. The media and inference flow described on this page remains synchronous.

See [Logging](logging.md) for level and target configuration, buffering, and flush behavior.

## What is an OpChain?

An OpChain is a smaller, self-contained micropipeline that runs locally within 'pekinfer'.

An OpChain usually contains:
- `InferenceController`
- `GenericImagePreprocess`
- one runtime-specific `Inference` Op
- `GenericPostprocess`

OpChains are defined in JSON and stored under `config/opchains/` or inside some model folders under `config/models/*/opchain.json`.

For most users, the important point is simple: the pipeline decides where media comes from, and the OpChain decides how a model is run.

## What is a model?

A model in PEK is made of two parts:

1. the actual model file, such as `.onnx` or `.hef`
2. a JSON descriptor, usually `model.json`, that tells PEK how to use it

The descriptor defines things such as:
- input tensor shape
- data layout such as `ImageRgbChw` or `ImageRgbHwc`
- normalization
- output behavior
- model family and content type

The same descriptor is used for local and published models. For a local model, `modelFile` is a
relative path beside the descriptor. For a published model, `modelFile` is its immutable canonical
`hf:...@...#file=...` locator. On first activation, the runtime uses modelfetch's pinned native C++
SDK to download and verify that one asset in the dedicated `var/models/` runtime store. No CLI
process or temporary request file is involved. Modelfetch owns the layout within that store and
returns the verified absolute path; AMP does not derive model-specific directories. Existing
verified content is reused. Manifest and bundle locators are not runtime entrypoints and are
rejected. `pek-menu` only launches pipelines; the descriptor path owns materialization for every
runtime consumer.

If you are only adding your own model, you usually only need to copy and adapt an existing `model.json`.

## Pipelines, OpChains, and models together

The normal runtime stack is:

1. a top-level pipeline is selected from `config/pipelines/`
2. that pipeline creates one or more `pekinfer` elements
3. each `pekinfer` reads its OpChain descriptor and registers with downstream controls
4. the first active frame queues OpChain setup on a background worker and continues downstream
5. each published model descriptor is downloaded and verified on demand
6. once setup is complete, later frames execute the ready OpChain synchronously
7. postprocessing writes structured results
8. downstream elements render, track, or publish those results

Setup is attempted once per activation. A setup failure is reported as a warning while the element
continues in pass-through mode, so one unavailable model does not stop video delivery. Disable and
re-enable the model to retry after fixing a transient network, authentication, or runtime problem.
If a model is disabled while setup is already running, that setup is allowed to finish and its ready
result is cached for the next activation, which prevents repeated partial downloads. Pipeline teardown
does request cooperative cancellation: the common synchronous setup contract propagates a C++ stop
token to model materialization, and modelfetch aborts at its next progress callback. Backend-specific
initialization that has already started may still need to return before teardown can complete.

The model-loading API remains synchronous and accepts an optional `ModelLoadContext` for cancellation.
Each OpChain setup creates one `OpSetupContext`, which carries that control to the operation that owns
the `modelDescriptor` attribute. Inference operations resolve the descriptor
through that context and pass the resulting local model path to their backend. Successful descriptor
resolutions are reused within the same setup, so one operation cannot trigger a context-free second
download. Thread and retry policy stay in the consumer: `pekinfer` schedules the same setup call on its
worker, while direct runtime and benchmark callers can continue to load synchronously without managing
a background thread.

## Runtime input expectations

The current video-oriented elements generally expect BGRA frames before preprocess and overlay stages.

That is why many pipeline presets contain lines such as:

```text
videoconvert ! video/x-raw,format=BGRA !
```

If this conversion is missing, inference or overlay behavior may not match expectations.

## How to use your own images or videos

The simplest method is to edit a pipeline preset under `config/pipelines/`.

### Custom image

For a still image, use a `filesrc` source followed by image decode and `imagefreeze`, for example:

```text
filesrc location=/work/data/images/my-image.jpg !
jpegdec !
imagefreeze !
videoconvert ! video/x-raw,format=BGRA !
```

Suggested path for custom images is `data/images/`.

### Custom video

For a video file, use a file source with decode, for example:

```text
filesrc location=/work/data/videos/my-video.mp4 !
decodebin name=dec
dec. ! queue ! videoconvert ! videoscale ! video/x-raw,format=BGRA !
```

Suggested path for custom videos is `data/videos/`.

### Custom camera

For live input, replace the source section with a camera source such as `v4l2src` or `libcamerasrc`, following the examples already stored in `config/pipelines/`.
For ready-to-run live camera presets, use `05-full-onnx-raspicam` for a Raspberry Pi camera or `06-full-onnx-usb-cam` for a USB camera at `/dev/video0`.

## How to use your own model

The normal user path is:

1. add a new folder under `config/models/`
2. place the model file there, or identify an immutable published asset
3. copy and adapt `model.json`, using a canonical file locator in `modelFile` for a published asset
4. copy and adapt `opchain.json`
5. point a pipeline preset to that model or OpChain

If your model output already matches an existing postprocessor, this can usually be done without changing the C++ code.

If it does not match an existing postprocessor, you will usually need to add your own postprocessor in the source tree. In practice, that means the task is no longer just about dropping files into `config/`.

## Common pipeline presets

The checked-in pipeline presets live under `config/pipelines/`.

Common presets include:

- `01-full-onnx.json` - integrated ONNX model pipelines.
- `02-full-onnx-hailo8.json` - integrated ONNX and Hailo 8 pipelines.
- `03-full-onnx-hailo8l.json` - integrated ONNX and Hailo 8L pipelines.
- `04-full-onnx-hailo10.json` - integrated ONNX and Hailo 10 pipelines.
- `05-full-onnx-raspicam.json` - integrated ONNX pipelines on the Raspberry Pi camera source.
- `06-full-onnx-usb-cam.json` - integrated ONNX pipelines on the USB camera source at `/dev/video0`.
- `cam-connect.json` - camera-contact demo.
- `gaze-detection.json` - gaze-estimation demo.
- `tracker-pc.json` - ONNX tracking demo.
- `tracker-rpi-hailo8.json` - Hailo 8 tracking demo.
- `tracker-rpi-hailo10.json` - Hailo 10 tracking demo.

Pipeline files often contain `alternative-source-*` and `alternative-sink-*` sections. Use those as templates when switching from the default sample media to a camera, video file, or different sink.

## Debugging from VS Code

After a successful build, the VS Code **Run and Debug** view can launch the most recent pipeline or prompt for a pipeline selection.

Use:

- **PEK Debug latest** to debug the last selected pipeline.
- **PEK Debug selection** to choose a pipeline before debugging.

## Where to look next

- [Bring Your Model](../how-to/bring-your-model.md)
- [Structural Basics](structural-basics.md)

## What should you have at the end of this document?

By the end of this page, you should have:

- a working mental model of the difference between pipelines, OpChains, and models
- a clear idea of where to change images, videos, cameras, or model descriptors
- an understanding of the normal end-to-end runtime flow inside PEK

Success looks like this: you can inspect a runtime issue or integration task and quickly decide whether the change belongs in a pipeline preset, an OpChain, or a model descriptor.

[Back to Concepts](/concepts)
