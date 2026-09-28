---
sidebar_position: 8
sidebar_label: Types
---
<!-- SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates -->


# Types, Tensor Metadata, and Shape

These types define the low-level contract between tensor builders, inference
backends, postprocessors, and Ops. They describe tensor element storage, semantic
layout, shape, inference context, and safe access to raw tensor memory.

## Tensor Element Types

`Tdt` describes the element type stored in tensor memory. Supported values cover
integer and floating-point tensors used by the current backends, including
`Uint8`, `Int8`, `Float16`, `Float32`, and `Int64`.

`getValueTypeByteSize(Tdt)` returns the element size in bytes. Callers use it with
shape-derived element counts to compute required tensor buffer sizes.

## Quantization And Normalization

`QuantizationArgs` stores `scale` and `zeroPoint` for quantized tensors. It gives
builders and parsers a shared representation for quantization and dequantization
without backend-specific branches.

`MeanStd` stores image normalization vectors. It also provides default checks so
identity normalization can be skipped.

## Tensor Roles And Data Kind

`TensorInOut` marks whether metadata applies to a model input or output.

`DataKind` describes what tensor bytes mean, not just how large each element is.
It distinguishes image layouts such as RGB CHW, RGB HWC, BGRA HWC, grayscale,
and scalar/vector values. Builders and parsers use `DataKind` to decide how to
interpret and transform tensor memory.

## Shape

`opk::Shape` is the fixed-capacity tensor dimension representation. It stores up
to eight dimensions and uses `-1` for dynamic dimensions.

Shape is a correctness boundary because it determines:

- tensor element count and required byte size
- compatibility between producers and consumers
- indexing assumptions in builders and parsers
- dynamic shape resolution during model loading

`getFullValueCount()` computes the element count. `isValid()` rejects empty,
zero-sized, or invalid negative dimensions. Dynamic dimensions can be resolved
against a concrete shape when backend metadata and descriptor metadata are merged.

## Inference Context

`ImageInferenceMetadata` records source and model image geometry needed to map
model outputs back to source coordinates.

`InferenceInfo` carries higher-level execution context such as parent UUID,
content type, and image metadata. Postprocessors should treat it as the
coordinate and provenance context for output interpretation.

## PixelRect

`PixelRect` represents an integer crop region in source pixels. It is used for
crop scheduling, preprocessing selection, and parent/child mapping between source
regions and derived detections.

## TensorView

`opk::TensorView` is a lightweight, non-owning view over contiguous tensor memory.
It wraps raw data with shape, element type, byte count, and quantization
parameters.

The view lets parsers consume tensor values as `float` through `get(size_t i)`,
regardless of whether the backend produced integer or floating-point storage.
It assumes flat contiguous memory; callers remain responsible for
multi-dimensional index mapping and for validating that the buffer is large
enough for the declared shape and element type.

`TensorView` is the primary boundary between inference backends and tensor parsers.
Used together with `Shape` and `InferenceInfo`, it keeps output parsing
backend-agnostic.
