---
title: Optimize A CPU Pipeline
sidebar_position: 7
sidebar_label: Pipeline Optimization
description: Choose pixel formats, model inputs, model size, thread count, and tracking strategy for CPU-only OPK pipelines.
---
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


# Optimize A CPU Pipeline

OPK performance depends on the whole pipeline shape, not on one setting. The
main cost is usually inference, but pixel format conversion and tensor
preprocessing can become visible when several networks run on the same frame.

Use this page as a checklist when building a CPU-only pipeline.

## Start From The Required Result

Choose the pipeline pieces for the problem you need to solve:

- source format and resolution
- one or more models
- tracker usage
- output format and sink

OPK provides these as reusable blocks. A fast pipeline usually comes from
combining the right blocks, not from forcing every pipeline through the same
format or model set.

Measure after each change. Use `opkperformance` for pipeline-visible timing and
use [Performance Measurement With Performix](performance-measurement.md) when
you need target-level CPU measurements.

## Supported Video Formats

The current video-oriented elements support these raw pixel formats:

- `BGRA`
- `RGB`
- `I420`
- `NV12`
- `YUY2`

This applies to the main OPK video elements such as `opkinfer`, `opkperformance`,
`opkosd`, and `opksink`.

## Supported Image Tensor Formats

Model descriptors define the input tensor format. The generic image preprocessor
can build these common model inputs from the supported video formats:

- `ImageRgbChw` with `Float32` or `Float16`
- `ImageRgbHwc` with `Uint8`, `Float32`, or `Float16`
- `ImageGray` with `Uint8` or `Float32`

Most vision models still expect RGB input tensors. This matters when the video
frame is kept in YUV, because each RGB model input still needs YUV-to-RGB work
during tensor generation.

## BGRA Or RGB Early In The Pipeline

Many existing presets convert the decoded source to `BGRA` near the beginning:

```text
videoconvert ! video/x-raw,format=BGRA !
```

This can look wasteful, especially when the source is already YUV and the output
will be encoded again. The benefit is that later inference stages can build RGB
HWC or CHW tensors from an RGB-like source. In multi-network pipelines, paying
one early conversion can be faster than repeating YUV-to-RGB preprocessing for
each model.

Prefer an early `BGRA` or `RGB` conversion when:

- several RGB models run on the same frame
- tensor preprocessing dominates the non-inference cost
- the pipeline needs overlays or other processing that benefits from packed RGB
  pixels

## Keep The Original YUV Format When It Helps

Keeping `I420`, `NV12`, or `YUY2` through the pipeline can avoid full-frame
conversions such as:

- `I420` to `BGRA` before inference
- `BGRA` back to `I420` before VP8 encoding

This is useful for simple or encode-heavy pipelines where there are only a few
inference stages. It keeps memory bandwidth lower and can preserve a format
closer to what video encoders already use.

It is not always faster. If several models need RGB tensors, the pipeline may
perform YUV-to-RGB conversion repeatedly during tensor generation. In cascaded or
multi-inference pipelines, that repeated cost can outweigh the saved full-frame
conversion.

## Use Grayscale Models When Accuracy Allows It

Grayscale input can be a fast path, especially from YUV sources where the luma
plane already carries the intensity information. A grayscale model can avoid
building a full RGB tensor.

Use this only when the model quality is acceptable for the task. Smaller or
grayscale models can be much faster, but they may detect fewer objects, classify
less accurately, or be less robust on difficult frames.

## Reduce Inference Cost First

Inference is usually the largest CPU cost. The biggest wins normally come from:

- choosing a smaller model
- reducing input resolution where quality still holds
- using grayscale models where color is not needed
- limiting the number of active models in the pipeline
- using model-specific postprocessing limits such as maximum detection count when
  the parser supports it

Treat model replacement as an accuracy and maintenance trade-off. Random model
files are not free: they need descriptors, preprocessing, postprocessing,
testing, and long-term support.

## Tune Runtime Threading Per Target

The ONNX path currently uses intra-op threading for vision models, sets inter-op
threading to one, and enables graph optimization. The best CPU thread count is
target-dependent: a desktop CPU, an Apple Silicon host, and a Raspberry Pi can
prefer different values.

When an inference element setting is available for thread count, tune it per
model and per target. Do not assume that the highest thread count is fastest.
Measure end-to-end FPS and per-scope timings, because too many inference threads
can starve preprocessing, tracking, encoding, or the UI path.

## Use Tracking To Skip Some Inference

`opkinfer` has experimental QoS-aware inference skipping through
`qos-enabled=true`. When enabled with sink QoS feedback, it can skip inference on
late frames while still forwarding buffers with `FrameResultsMeta`.

This works best together with `opktracker`: the tracker can keep emitting
prediction-only results when a fresh inference result is not available. The
trade-off is that tracking costs CPU time too, and quality depends on how often
real detections are still produced.

Use this pattern when:

- a detector is expensive
- missing an occasional detection frame is acceptable
- tracked boxes are good enough between inference frames

Do not use it as a universal fix. If inference is skipped too aggressively,
objects can be missed before the tracker ever sees them.

## Practical Decision Guide

For a single model or encode-heavy pipeline, try keeping the source in its
original YUV format and measure.

For several RGB models on the same frame, try converting once to `BGRA` or `RGB`
near the beginning and measure.

For face detection or other tasks where color is not essential, try a grayscale
model and compare quality.

For overloaded pipelines, first reduce inference cost with smaller models,
smaller inputs, or fewer active models. Then try tracker-assisted skipping.

For final tuning, adjust thread count on the target hardware and validate the
whole pipeline, not just the inference call.

[Back to How-To Guides](/how-to)
