---
sidebar_position: 14
sidebar_label: Tensor Builder
---

# TensorBuilder

`TensorBuilder` is the interface for constructing model input tensors from media
sources. The common image builder converts image regions into tensor buffers with
the layout, size, normalization, and quantization expected by the model.

## Purpose

Image preprocessing usually combines a small set of operations:

- color or pixel format conversion
- HWC/CHW layout conversion
- resize and optional interpolation policy
- crop or region extraction
- optional letterboxing
- value scaling, mean/std normalization, and quantization handling

`GenericImageTensorBuilder` covers the common image path. Specialized builders can
be added for models that need custom transforms or multi-stage derived inputs.

## Setup Contract

`TensorBuilder::Setup` describes both source and destination memory.

`ImageSource` provides the input pointer, source dimensions, region of interest,
source `DataKind`, element type, and normalization parameters.

`ImageDestination` provides the output pointer, destination dimensions, target
region, destination `DataKind`, and element type.

The source and destination regions allow crop extraction and packing into
preallocated tensor memory without unnecessary intermediate buffers.

## Execution Contract

`build(const Setup&)` reads the configured source region, applies preprocessing,
writes the destination tensor bytes, and returns a `Result`.

The builder does not own source or destination memory. Callers must keep both
valid for the duration of the build call.

## Audio Support

`AudioSource` and `AudioDestination` are placeholders. The interface reserves the
shape needed for future audio tensor building, but no audio preprocessing
implementation is provided yet.
