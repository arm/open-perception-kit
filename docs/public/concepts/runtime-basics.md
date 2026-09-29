---
title: Runtime Basics
sidebar_position: 2
sidebar_label: Runtime Basics
description: Understand how GStreamer pipelines, OpChains, model descriptors, and FrameResults fit together at runtime.
---
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


# Runtime Basics

This page explains the practical runtime concepts you need when you want to run OPK with your own media or your own model files.

## What will you learn from this documentation?

If you follow this page successfully, you will learn how OPK uses pipelines, OpChains, and model descriptors together at runtime.

At the end of this page, you should be able to tell where to change the media source, where to change model execution behavior, and what a successful runtime path looks like from input to rendered result.

## GStreamer basics in OPK

OPK runs inside GStreamer pipelines.

A GStreamer pipeline is a chain of elements connected with `!`, for example:

```text
source ! convert ! process ! sink
```

In OPK, a typical video pipeline looks like this:

```text
source ! videoconvert ! opkinfer ! opkosd ! opksink
```

The source provides media, `opkinfer` runs AI processing, `opkosd` can draw server-side overlays when enabled, and `opksink` publishes the result.

## What is a pipeline?

In this repository, “pipeline” usually means the top-level GStreamer runtime assembled from a JSON preset under `config/pipelines/`.

Those presets define:
- the source, such as an image, video file, or camera
- the processing elements used in the stream
- the sink, usually `opksink`

Pipeline presets, OpChains, and model descriptors each require a
`"version": "1.0.0"`-style string. A major mismatch fails, a minor mismatch warns
and continues, and patch differences are ignored at runtime. Increment the
file's patch whenever editing it within the same major/minor contract.
See [Configuration Compatibility](configuration-compatibility.md) for version
ownership, dependency requirements, and migration from unversioned presets.

The launcher `tools/opk-menu` reads these presets and runs them in-process through
the C++ Runtime. The printed `gst-launch-1.0` line is a copyable description for
diagnostics; `opk-menu` does not start a separate `gst-launch-1.0` process. This
keeps the selected pipeline in the launcher process when it is attached to a
debugger. For normal use, start it through the VS Code run tasks; for terminal
use, call `./tools/opk-menu` from the active host side container or remote host
container terminal at the project root.

Pipeline presets can set `loop` to `true` for finite, seekable media. The Runtime
then loops the GStreamer time segment without recreating the pipeline. A preset
needs at least one finite, seekable terminal media branch; purely live and
non-seekable pipelines cannot use this option. Element or Op state remains alive
between loop iterations until the pipeline is stopped.

`Pipeline::StartOptions` also controls the process-wide OPK log level and the
stdout, stderr, and file log targets applied when playback starts. Its defaults
select the `Error` level and stderr only. `opk-menu` accepts `--log-level` and
`--log-targets` to override those defaults for a selected pipeline, for example
`./tools/opk-menu --log-level debug --log-targets stdout,stderr yolo26n-320`.
Because the logger is process-wide, starting a second pipeline with different
options replaces the logging settings used by the first one as well.

If you want to change which image, video, or camera is used, this is usually the first place to edit.

Some checked-in presets intentionally set `opkinfer active=false`.
That lets the OPK web UI register the model first and then enable it from the **Model Selector** panel when you are ready.
`active=false` disables per-frame OpChain execution; it does not defer setup.
`opkinfer` still loads the OpChain and its model during startup, so every
referenced model artifact must already exist.

At the moment, pipeline execution is synchronous end to end. An asynchronous inference execution flow is planned for a later update, but it is not available yet.

## Logging does not make inference asynchronous

OPK logging uses a separate worker thread so normal logging calls do not perform output I/O on the
calling component's thread. This is independent of GStreamer pipeline scheduling and inference
execution. The media and inference flow described on this page remains synchronous.

See [Logging](logging.md) for level and target configuration, buffering, and flush behavior.

## What is an OpChain?

An OpChain is a smaller, self-contained micropipeline that runs locally within 'opkinfer'.

An OpChain usually contains:
- `InferenceController`
- `GenericImagePreprocess`
- one runtime-specific `Inference` Op
- `GenericPostprocess`

OpChains are defined in JSON and stored under `config/opchains/` or inside some model folders under `config/models/*/opchain.json`.

For most users, the important point is simple: the pipeline decides where media comes from, and the OpChain decides how a model is run.

## What is a model?

A model in OPK is made of two parts:

1. the actual model file, such as `.onnx` or `.pte`
2. a JSON descriptor, usually `model.json`, that tells OPK how to use it

The descriptor defines things such as:
- input tensor shape
- data layout such as `ImageRgbChw` or `ImageRgbHwc`
- normalization
- output behavior
- model name and content type

`modelFile` is a local filesystem path. Relative paths are resolved from the
directory containing its Model descriptor; absolute paths are used unchanged. Relative
and absolute paths retain their components so the filesystem resolves symlinks
and parent traversal in the normal order. Relative paths are preferred so model
folders remain portable. The standard
container images try to download published OPK model artifacts from pinned
Hugging Face revisions into those paths. An optional `hfDownload.sha256` digest
is verified before the artifact is atomically installed. When `HF_TOKEN` is unset, accessible
public artifacts download anonymously. Each failed download is logged and
skipped, so the container build can succeed with an incomplete model set.
Runtime containers do not download models and need no Hugging Face network
access or credentials. A pipeline that references a missing artifact fails
during OpChain setup, including when its `opkinfer` starts with `active=false`.
Build-time `hfDownload` staging requires a descriptor-relative `modelFile`
inside the model folder; absolute paths remain available for externally managed
runtime artifacts.

If you are only adding your own model, you usually only need to copy and adapt an existing `model.json`.

## Pipelines, OpChains, and models together

The normal runtime stack is:

1. a top-level pipeline is selected from `config/pipelines/`
2. that pipeline creates one or more `opkinfer` elements
3. each `opkinfer` loads an OpChain
4. the OpChain loads one or more model descriptors
5. postprocessing writes structured results
6. downstream elements render, track, or publish those results

## Runtime input expectations

The current video-oriented elements can negotiate `BGRA`, `RGB`, `I420`, `NV12`,
and `YUY2`. Many checked-in presets still convert to `BGRA` before preprocess
and overlay stages because it is the conservative known-good path.

That is why many pipeline presets contain lines such as:

```text
videoconvert ! video/x-raw,format=BGRA !
```

If you remove this conversion, measure the result and check that the selected
models and overlay path still behave as expected.

## How to use your own images or videos

The simplest method is to edit a pipeline preset under `config/pipelines/`.

### Custom image

For a still image, use a `filesrc` source followed by image decode and `imagefreeze`, for example:

```text
filesrc location="${OPK_PROJECT_ROOT:-/work}/data/images/my-image.jpg" !
jpegdec !
imagefreeze !
videoconvert ! video/x-raw,format=BGRA !
```

Suggested path for custom images is `data/images/`.

### Custom video

For a video file, use a file source with decode, for example:

```text
filesrc location="${OPK_PROJECT_ROOT:-/work}/data/videos/my-video.mp4" !
decodebin name=dec
dec. ! queue ! videoconvert ! videoscale ! video/x-raw,format=BGRA !
```

Suggested path for custom videos is `data/videos/`.

### Custom camera

For live input, replace the source section with a camera source such as `v4l2src` or `libcamerasrc`, following the examples already stored in `config/pipelines/`.
For ready-to-run live camera presets, use `full-onnx-raspicam` for a Raspberry Pi camera or `full-onnx-usb-cam` for a USB camera at `/dev/video0`.

## How to use your own model

The normal user path is:

1. add a new folder under `config/models/`
2. place the model file there for local development, or add an `hfDownload`
   object when the container build must include it
3. copy and adapt `model.json`
4. copy and adapt `opchain.json`
5. point a pipeline preset to that model or OpChain

If your model output already matches an existing postprocessor, this can usually be done without changing the C++ code.

If it does not match an existing postprocessor, you will usually need to add your own postprocessor in the source tree. In practice, that means the task is no longer just about dropping files into `config/`.

## Common pipeline presets

The checked-in pipeline presets live under `config/pipelines/`. Common presets include:

- `full-onnx.json` - all supported ONNX releases on bundled video.
- `full-onnx-raspicam.json` - all supported ONNX releases on a Raspberry Pi camera.
- `full-onnx-usb-cam.json` - all supported ONNX releases on a USB camera at `/dev/video0`.
- `full-onnx-yuv.json` - all supported ONNX releases on bundled video with the decoded pixel format preserved.
- `yolo26n-320.json` - YOLO26n-320 on bundled video.
- The remaining focused presets run one release or its required detector cascade.

Pipeline files often contain `alternative-source-*` and `alternative-sink-*` sections. Use those as templates when switching from the default sample media to a camera, video file, or different sink.

## Debugging from VS Code

After a successful build, the VS Code **Run and Debug** view can launch the most recent pipeline or prompt for a pipeline selection.

Use:

- **OPK Debug latest** to debug the last selected pipeline.
- **OPK Debug selection** to choose a pipeline before debugging.

## Where to look next

- [Bring Your Model](../how-to/bring-your-model.md)
- [Structural Basics](structural-basics.md)

## What should you have at the end of this document?

By the end of this page, you should have:

- a working mental model of the difference between pipelines, OpChains, and models
- a clear idea of where to change images, videos, cameras, or model descriptors
- an understanding of the normal end-to-end runtime flow inside OPK

Success looks like this: you can inspect a runtime issue or integration task and quickly decide whether the change belongs in a pipeline preset, an OpChain, or a model descriptor.

[Back to Concepts](/concepts)
