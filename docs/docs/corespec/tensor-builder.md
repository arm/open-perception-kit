---
sidebar_position: 16
sidebar_label: Tensor Builder
---

# TensorBuilder
## Parameterized Input Tensor Construction Interface

TensorBuilder defines the interface responsible for constructing model input
tensors from media sources.

It is primarily used to convert image regions into correctly formatted tensor
buffers suitable for inference runtimes. The builder encapsulates common input
preprocessing steps such as format conversion, layout transformation, cropping,
scaling, normalization, and region mapping.

Audio support is reserved in the interface but is not implemented yet.

---

# Purpose

Inference runtimes typically require tightly specified tensor memory layouts.
TensorBuilder implementations provide a reusable, parameter-driven mechanism
to populate these tensors from pipeline media buffers.

For image inputs, the intent is that a single well-designed, highly
parameterizable builder can cover the majority of common model input
requirements across computer vision workloads, because most preprocessing
pipelines reduce to a combination of:

- Color/pixel format conversion
- Tensor layout conversion (HWC/CHW)
- Resize (with selectable interpolation policies)
- Crop and region extraction
- Letterboxing (aspect-ratio preservation with padding)
- Value scaling and normalization (mean/std, range mapping, quantization)

However, some models still require specialized preprocessing that may not fit
a generic builder (for example: custom color transforms, non-linear tone
mapping, per-channel affine transforms beyond mean/std, specialized geometry,
or multi-stage derived inputs). The architecture therefore supports adding
specialized builders where needed.

The **GenericImageTensorBuilder** class does this generic image tensor building.

---

# Setup Contract

`TensorBuilder::Setup`

Setup contains both the source description and the destination tensor target.
It provides the full parameter set required for preprocessing and packing.

## ImageSource

ImageSource describes the input buffer and how to interpret it:

- Pointer to the source memory block
- Source surface dimensions and a region of interest (ROI)
- DataKind and element type describing pixel representation
- Normalization parameters (mean / std) for value standardization

The source ROI allows building tensors from cropped subregions of a larger
surface without duplicating intermediate buffers.

## ImageDestination

ImageDestination describes the destination tensor memory block and expected
layout:

- Pointer to the destination memory block
- Destination surface dimensions and destination region placement
- DataKind and element type describing output tensor representation

Destination ROI enables packing into subregions when needed (for example,
building tiled or batched layouts, or writing into preallocated tensor memory).

---

# Execution Contract

`build(const Setup&)`

The build method:

- Reads from the configured source region.
- Applies the configured preprocessing and packing steps.
- Writes the resulting tensor bytes into the provided destination buffer.
- Returns a Result indicating success or failure.

The builder does not own source or destination memory.
Callers must ensure memory validity for the duration of the build call.

---

# Audio Support

`AudioSource` and `AudioDestination` are currently placeholders.
The interface reserves the structure needed for future audio tensor building,
but no audio preprocessing implementation is provided yet.
