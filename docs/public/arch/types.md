---
sidebar_position: 8
sidebar_label: Types
---

# Types, Tensor Metadata, and Shape
## Core Primitives for Tensor I/O and Inference Contracts

These headers define the low-level primitives used across the AMP inference stack:
value types, tensor element types, data layout kinds, inference metadata, and tensor shapes.

They sit on the hot path of preprocessing → inference → postprocessing, and they define
the “contract surface” between components (builders, backends, parsers, and Ops).

# Value Types and Tensor Element Types

The code defines a minimal set of numeric aliases used by tensor code:

- `Uint8`, `Int8`
- `Float16`, `Float32`
- `Int64`

`Float16` is implemented as an architecture-dependent half type:

- ARM / AArch64: `__fp16`
- x86_64: `_Float16`

This keeps half-precision buffers “native” where possible, and avoids depending on
a third-party half implementation.

## Tdt (Tensor Data Type)

`Tdt` specifies the element type stored in a tensor buffer:

- `Uint8`, `Int8`
- `Float16`, `Float32`
- `Int64`

In many libraries, the term dtype is commonly used for this purpose.
However, since the system already defines multiple concepts named type, 
a more specific, non-generic name was chosen to avoid ambiguity and confusion.

### Byte Size Lookup

`getValueTypeByteSize(Tdt type)` returns the element size in bytes.
This is used to compute byte counts for tensor buffers:

- element_count × byte_size = required buffer bytes

This function is one of the central correctness gates for safe tensor memory access.

# Quantization and Normalization Helpers

## QuantizationArgs

`QuantizationArgs` stores quantization parameters:

- `scale`
- `zeroPoint`

Typical uses:

- mapping float domain values into int8/uint8 tensors
- dequantizing output tensors for parsing

Even when not used in every backend, it standardizes how quantization parameters
are represented across the system.

## MeanStd

`MeanStd` provides checks for “default” normalization vectors:

- default mean = (0,0,0)
- default std  = (1,1,1)

These checks are used to avoid unnecessary work when normalization parameters
are effectively identity transforms (no mean subtraction, no scaling).

# Tensor Direction

## TensorInOut

`TensorInOut` is a small enum describing whether a tensor belongs to:

- `In`  (model input)
- `Out` (model output)

This is commonly used in metadata structures and model descriptors to keep
tensor properties scoped to input vs output roles.

# DataKind
## Semantic Data Layout / Interpretation of Tensor Memory

`DataKind` describes what the bytes in a tensor (or tensor-like buffer) represent.
This is *not* the same as `Tdt` (element type). `DataKind` captures layout and meaning.

### Image Layouts

- `ImageRgbChw`   : planar, CHW (RRR... GGG... BBB...)
- `ImageRgbHwc`   : interleaved, HWC (RGBRGBRGB...)
- `ImageBgraHwc`  : interleaved, HWC with alpha (BGRABGRA...)
- `ImageGray`     : single-channel grayscale

These kinds are essential for image tensor builders and preprocess Ops, because they
define how pixel data must be interpreted and transformed into model input tensors.

### Scalar / Vector Kinds

- `Value`, `Vector2`, `Vector3`, `Vector4`

These are used for configuration tensors, auxiliary inputs, or compact output forms.

`isScalarDataKind(DataKind kind)` returns true for the scalar/vector kinds,
enabling generic handling for non-image tensor inputs.

# Tensor Count Limits

- `MaxTensorCount = 4`

This defines the maximum number of tensor slots used by components like:

- inference Ops
- OpChainContext tensor arrays
- parsers that operate on multiple outputs

It is a deliberate fixed upper bound for predictable stack/struct sizing.
This constant is used wherever multiple tensors are referenced.

- `InvalidTensorIndex = 0xdead`

A sentinel used for invalid/uninitialized tensor index values.

# Inference Metadata
## InferenceInfo and ImageInferenceMetadata

Inference execution often requires more than raw tensor bytes.
Postprocessing frequently needs context to interpret results correctly.

## ImageInferenceMetadata

Captures the image geometry used by inference:

- `width`, `height`         : physical size of the image region inference ran on
- `modelWidth`, `modelHeight`: model input tensor spatial dimensions

This is used to:

- map output coordinates back to the source coordinate system
- interpret resizing, scaling, and potential letterboxing

## InferenceInfo

Holds higher-level inference execution metadata:

- `parentUuid`   : link to the Perception member (e.g., the originating frame/crop)
- `contentType`  : semantic meaning (“humanFace”, “genericObject”, etc.)
- `image`        : image-specific metadata (see above)

Parsers and postprocessors should treat this struct as the authoritative “context”
for interpreting output tensors, especially when coordinate normalization or crop-based
inference is involved.

---

# PixelRect
## Crop Rectangle Primitive

`PixelRect` represents an integer crop region in pixels:

- `x`, `y`, `width`, `height`

It is typically used for inference crop scheduling, preprocessing selection,
and mapping detections to specific crop windows.

---

# Shape
## Tensor Dimension Representation

`amp::Shape` represents a tensor’s dimensionality using a fixed-capacity design.

### Key Properties

- Max 8 dimensions (`valueCount[8]`)
- Active dimension count stored in `dimensionCount`
- Dynamic dimensions are encoded as `-1`

The fixed-size representation is efficient and allocation-free, which matters
because shapes are frequently inspected at runtime.

---

## Why Shape Is Critical for Tensor I/O

Shape is a correctness and safety primitive.

It determines:

- element count and therefore required buffer byte size
- compatibility checks between tensor producers and consumers
- how indexing/striding logic in builders and parsers must behave

A single shape mismatch can cause:

- out-of-bounds reads/writes
- corrupted outputs
- silent inference misinterpretation
- crashes that manifest far away from the root cause

In AMP, shape is part of the “contract” between:

- tensor builders (produce correctly shaped inputs)
- inference backends (expect exact input dimensions)
- tensor parsers (decode outputs with correct indexing)

---

## Core Operations

### `getFullValueCount()`

Returns the product of all dimensions.
Used to compute element count (and indirectly required byte count).

### `toString()`

Produces a readable dimension list.
Useful for error reporting and debugging model descriptor mismatches.

## Validation

### `isInvalid()` / `isValid()`

A shape is invalid if:

- `dimensionCount == 0`
- any dimension is `0`
- any dimension is `< -1`

This prevents zero-sized shapes and invalid negative sizes.
Dynamic dims (`-1`) are allowed.

---

## Dynamic Dimensions

### `hasDynamicDimension()`

Checks whether the shape contains a dynamic dimension (`-1`).

### `applyDimensionsForDynamic(const Shape& other)`

Resolves dynamic dimensions against a concrete shape:

- static dimensions must match
- dynamic (`-1`) dimensions are overwritten with `other`’s values
- dimension counts must match

This supports models with runtime-resolved input/output shapes while still enforcing
strict compatibility constraints.

---

# TensorView
## Non-Owning, Quantization-Agnostic Tensor Wrapper

`amp::TensorView` is a lightweight, non-owning view over a contiguous block of
tensor memory.

It is primarily intended for postprocessing and tensor parsers.  
It allows parsers to operate in a **quantization-agnostic** way, independent of
the underlying tensor storage type (INT8, UINT8, FP16, FP32, INT64).

The view does **not** own the underlying buffer.  
Memory lifetime must be managed by the producer (inference backend, Op, or
tensor builder).

---

## Construction

A `TensorView` is constructed with:

- `const void* data` — pointer to the raw tensor memory.
- `size_t byteCount` — total buffer size in bytes.
- `amp::Shape shape` — logical tensor shape.
- `amp::Tdt type` — element type descriptor.
- `float scale`, `float zeroPoint` — quantization parameters.

During construction:

- `typeSize` is derived from `Tdt`.
- `valueCount` is computed from `shape.getFullValueCount()`.

No internal validation ensures that `byteCount` matches
`valueCount * typeSize`.  
Such validation should be performed by the component constructing the view.

---

## Quantization-Agnostic Access

### `float get(size_t i) const`

Returns the `i`-th tensor element as a `float`, regardless of underlying storage type.

This abstraction allows tensor parsers to ignore quantization details entirely.
All tensor outputs can be consumed uniformly as floating-point values.

---

## Linear Indexing Model

`TensorView` assumes:

- Contiguous flat memory layout.
- Row-major linear indexing.

The caller is responsible for mapping multi-dimensional indices
(e.g., `(n, c, h, w)`) to linear indices.

`Shape` defines dimensional structure, but `TensorView` does not
perform multi-dimensional indexing itself.

---

## Accessors

- `getCount()` — number of tensor elements.
- `getByteCount()` — total raw buffer size.
- `getValueType()` — returns `Tdt`.
- `getShape()` — returns tensor shape.
- `getData()` — returns raw `uint8_t*` pointer.

These allow low-level consumers to inspect raw memory if needed.

## Design Principles

- **Non-owning view** — zero allocation, zero copying.
- **Backend-agnostic** — works with any inference runtime.
- **Quantization-agnostic** — integer and floating-point tensors are normalized to float.
- **Minimal overhead** — intended for high-frequency postprocessing paths.

---

## Role in Postprocessing

`TensorView` is the canonical interface between:

- Inference backends
- Op-based postprocessing
- Tensor parsers

By normalizing all tensor reads to `float`, it prevents:

- Quantization logic duplication
- Backend-specific parsing branches
- Storage-type conditionals in parser implementations

Correct use of `TensorView`, together with `Shape` and `InferenceInfo`,
ensures consistent and safe interpretation of inference outputs across
all supported runtimes.
