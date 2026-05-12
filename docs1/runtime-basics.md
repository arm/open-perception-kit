# Runtime Basics

This page explains the practical runtime concepts you need when you want to run AMP with your own media or your own model files.

## What will you learn from this documentation?

If you follow this page successfully, you will learn how AMP uses pipelines, OpChains, and model descriptors together at runtime.

At the end of this page, you should be able to tell where to change the media source, where to change model execution behavior, and what a successful runtime path looks like from input to rendered result.

## GStreamer basics in AMP

AMP runs inside GStreamer pipelines.

A GStreamer pipeline is a chain of elements connected with `!`, for example:

```text
source ! convert ! process ! sink
```

In AMP, a typical video pipeline looks like this:

```text
source ! videoconvert ! ampinfer ! amposd ! ampsink
```

The source provides media, `ampinfer` runs AI processing, `amposd` draws overlays, and `ampsink` publishes the result.

## What is a pipeline?

In this repository, “pipeline” usually means the top-level GStreamer runtime assembled from a JSON preset under `config/pipelines/`.

Those presets define:
- the source, such as an image, video file, or camera
- the processing elements used in the stream
- the sink, usually `ampsink`

The launcher `tools/amp-menu` reads these presets and runs them. For normal use, start it through the VS Code run tasks; for terminal use, call `./tools/amp-menu` from the active host side container or remote host container terminal at the project root.

If you want to change which image, video, or camera is used, this is usually the first place to edit.

Some checked-in presets intentionally set `ampinfer active=false`.
That lets the AMP web UI register the model first and then enable it from the **AI Models** panel when you are ready.

## What is an OpChain?

An OpChain is a smaller, self-contained micropipeline that runs locally within 'ampinfer'.

An OpChain usually contains:
- `InferenceController`
- `GenericImagePreprocess`
- one runtime-specific `Inference` Op
- `GenericPostprocess`

OpChains are defined in JSON and stored under `config/opchains/` or inside some model folders under `config/models/*/opchain.json`.

For most users, the important point is simple: the pipeline decides where media comes from, and the OpChain decides how a model is run.

## What is a model?

A model in AMP is made of two parts:

1. the actual model file, such as `.onnx` or `.hef`
2. a JSON descriptor, usually `model.json`, that tells AMP how to use it

The descriptor defines things such as:
- input tensor shape
- data layout such as `ImageRgbChw` or `ImageRgbHwc`
- normalization
- output behavior
- model family and content type

If you are only adding your own model, you usually only need to copy and adapt an existing `model.json`.

## Pipelines, OpChains, and models together

The normal runtime stack is:

1. a top-level pipeline is selected from `config/pipelines/`
2. that pipeline creates one or more `ampinfer` elements
3. each `ampinfer` loads an OpChain
4. the OpChain loads one or more model descriptors
5. postprocessing writes structured results
6. downstream elements render, track, or publish those results

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

## How to use your own model

The normal user path is:

1. add a new folder under `config/models/`
2. place the model file there
3. copy and adapt `model.json`
4. copy and adapt `opchain.json`
5. point a pipeline preset to that model or OpChain

If your model output already matches an existing postprocessor, this can usually be done without changing the C++ code.

If it does not match an existing postprocessor, you will usually need to add your own postprocessor in the source tree. In practice, that means the task is no longer just about dropping files into `config/`.

## Where to look next

- [Bring Your Model](bring-your-model.md)
- [Structural Basics](structural-basics.md)

## What should you have at the end of this document?

By the end of this page, you should have:

- a working mental model of the difference between pipelines, OpChains, and models
- a clear idea of where to change images, videos, cameras, or model descriptors
- an understanding of the normal end-to-end runtime flow inside AMP

Success looks like this: you can inspect a runtime issue or integration task and quickly decide whether the change belongs in a pipeline preset, an OpChain, or a model descriptor.
