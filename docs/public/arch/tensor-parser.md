---
sidebar_position: 15
sidebar_label: Tensor Parser
---

# TensorParser
## Output Tensor Interpretation Interface

TensorParser defines the interface responsible for converting raw inference
output tensors into structured perception metadata.

The parser operates on tensor memory produced by an inference runtime and
translates the raw numerical outputs into domain-specific information that
can be stored in the Perception model.

TensorParser implementations are backend-agnostic and focus solely on
interpreting model outputs.

---

# Purpose

Inference runtimes produce raw tensor data.
TensorParser implementations interpret this data and generate structured
metadata suitable for downstream processing.

Typical responsibilities include:

- Interpreting tensor layouts
- Decoding detection outputs
- Extracting classification results
- Converting model-specific formats into framework-standard metadata
- Writing results into a Perception layer

---

# Input Structure

`TensorParser::Input`

The Input structure aggregates all contextual information required for
parsing inference outputs.

It provides:

- Access to output tensor memory blocks
- Inference metadata describing execution context
- Attribute configuration for parser customization
- The target Perception layer context (Perception::Layer instance)

The parser does not own tensor memory and must treat all tensor pointers
as externally managed.

---

# Execution Contract

`parse(const Input&, Perception::Layer&)`

The parse method:

- Reads tensor memory from the provided Input.
- Interprets the model outputs according to implementation logic.
- Writes structured results into the provided Perception layer.
- Returns a Result indicating success or failure.

The parser must not modify tensor memory.
The parser is responsible only for interpretation and metadata generation.

---

# Architectural Role

TensorParser forms the boundary between:

- Low-level inference tensor memory
- High-level perception metadata representation

This separation ensures:

- Clear responsibility boundaries
- Reusable inference backends
- Model-specific parsing isolation
- Clean extensibility for new model types

New models can be integrated by implementing a corresponding TensorParser
without modifying the core execution engine.

The default parsers are located in **amp-std-ops**.

---

# Implemented Tensor Parsers

The following TensorParser implementations are currently available.
Each parser converts model-specific output tensors into structured
Perception metadata.

---

## YOLO Parser

Parses object detection outputs produced by YOLO-based models.
Extracts bounding boxes, class identifiers, and confidence scores.
Writes detected objects into the Perception layer.

There is also a parser for HailoRT-accelerated YOLO output when the network handles NMS differently.

## UltraFace Parser

Parses face detection outputs from UltraFace models.
Extracts face bounding boxes and associated confidence values.
Optimized for lightweight and real-time face detection scenarios.

## Gaze Detector Parser

Parses the output of the gaze estimation model by Arm.
Interprets directional vector yaw/pitch values as gaze angles.
Attaches gaze-related metadata to the Perception layer.

## Camera Contact Parser

Parses binary camera-contact logits with shape `[1, 2]`.
Converts the two logits into a single top-1 classification result.
Attaches a `cameraContact` classification to the parent face crop.

## Paddle OCR Detector Parser

Parses text detection outputs from Paddle OCR detection models.
Extracts a text-region segmentation map.
Stores detected text regions for downstream recognition stages.

## ImageNet Classification Parser

Parses classification outputs from ImageNet-style models.
Extracts top-k class predictions and confidence scores.
Attaches classification results to the Perception layer.

## Person Classification Parser

Parser for the person classification network of the Arm AAIR team.
Stores a person-classification detection object in the layer.

## ModNet Segmentation Parser

Parses segmentation-mask outputs into `Perception::SegmentationMap`.
Used when the downstream runtime should render or publish a mask.

## RVM Parser

Parses RVM foreground-mask output into `Perception::SegmentationMap`.
This is another segmentation-style path that shares the same `contentType`.

## Object Embedding Parser

Parses embedding outputs into `Perception::ObjectEmbedding`.
This is typically used together with `amptracker` or other ReID-style flows.

## Dummy Parser

Debug parser that logs tensor information without producing a meaningful application result.

# Additional Parsers

Additional parsers can be implemented by conforming to the TensorParser
interface without modifying the core execution engine.

Today, custom parsers are implemented in C++ under `development/ops-std/postproc/`
and selected by name from `GenericPostprocessOp.cpp`.
That keeps model-specific output handling close to the existing OpChain and parser layer.

Python-based postprocessing is not part of the current runtime.
It is discussed as a future direction in [Known limitations](known-limitations.md).

![Postprocessor types](../../static/img/postprocessor-types.png)

This design still keeps custom parsing localized: adding a new parser usually means
adding one parser class, registering it, and referencing it from `opchain.json`.
