---
sidebar_position: 6
sidebar_label: pek::Model
---

# Perception Experience Kit Model System
## Engine-Agnostic Model Abstraction and Descriptor Merge Architecture

The Perception Experience Kit inference framework separates model runtime introspection from
user-provided model metadata.

This separation ensures:

-   Backend independence (ONNX, HailoRT, ExecuTorch, etc.)
-   Strict validation of tensor definitions
-   Deterministic runtime behavior
-   Clean integration into OpChains inside pekinfer

Two primary components are involved:

-   pek::Model (engine-agnostic runtime model representation)
-   pek::ModelDescriptor (declarative JSON metadata)

The final runtime model used by inference Ops is an pek::Model instance
produced by merging:

1.  Information extracted from the inference engine
2.  Information provided in the JSON descriptor

If overlapping information is present in both sources, it must match.
Otherwise, model loading fails.

# 1. pek::Model

## Canonical Runtime Representation

The pek::Model class is the unified runtime model abstraction used by the Op
system. It defines the input and output tensors used when inference is executed.

It contains:

-   Input tensor definitions
-   Output tensor definitions
-   Tensor shapes
-   Value types
-   Quantization parameters
-   Data formats (DataKind)
-   Model family and content type
-   Dynamic output configuration

It is fully engine-agnostic.

Once constructed, this object is passed to the inference Op, which can
then be inserted into an OpChain.

## 1.1 Model Inputs

Each input is represented by ModelInput.

Contains:

-   name
-   DataKind
-   Tdt (tensor data type)
-   Shape
-   QuantizationArgs
-   Normalization parameters (mean, std)
-   Optional batch size

Supported DataKind values:

-   ImageRgbChw
-   ImageRgbHwc
-   ImageBgraHwc
-   ImageGray

Tensor formats for inference configuration:

-   Value
-   Vector2
-   Vector3
-   Vector4

This allows the system to handle:

-   Image tensors
-   Scalar/vector configuration inputs

## 1.2 Model Outputs

Each output is represented by ModelOutput.

Contains:

-   name
-   Shape
-   Tdt (dtype - tensor data type)
-   QuantizationArgs

The model may operate in:

-   Static output mode
-   Dynamic output mode (`useDynamicOutput`) when the selected runtime resolves output shapes at execution time.

# 2. ModelDescriptor

## Declarative JSON Model Metadata

ModelDescriptor is a JSON-based configuration object that describes
model metadata and runtime expectations.

It contains:

-   Model identity
-   Model file path
-   Model family
-   Content type
-   Input tensor metadata
-   Output tensor metadata
-   Dynamic output configuration

## Example JSON Descriptor

```
{
	"name": "yolo",
	
	"modelFamily": "yolo-obj",
	"contentType": "genericObject",
	"modelFile": "yolo11n-fp32-320.onnx",
	
	"dynamicOutput": true,
	
	"inputTensors": 
	[
		{
			"shape": [ 1, 3, 320, 320 ],
			"dataKind": "ImageRgbChw"
		}
	]
}
```

The descriptor may define:

-   Explicit tensor shapes
-   Data format (dataKind)
-   Value types
-   Quantization overrides
-   Normalization parameters
-   Scalar/vector input configuration

# 3. Model Loading Lifecycle

Model loading is a two-phase process.

## Phase 1 - Engine Introspection

The selected inference engine:

-   Loads the model file
-   Extracts tensor names
-   Extracts tensor shapes
-   Extracts value types
-   Extracts quantization parameters

An initial pek::Model instance is created from engine data. 
Most of these values are not provided directly by every model file.

## Phase 2 - Descriptor Merge

pek::Model::applyModelFromDescriptor merges JSON metadata into the
engine-derived model.

Input tensor validation rules:

-   Tensor count must match
-   DataKind must not be Unknown
-   JSON cannot contain dynamic dimensions
-   If engine shape is dynamic, descriptor must resolve it
-   If engine shape is static and descriptor provides shape, shapes must
    match

Output tensor validation rules:

If dynamicOutput is false:

-   Output tensor count must match
-   Shapes must match unless resolved from engine dynamic shape

If dynamicOutput is true:

-   JSON must not define output tensors
-   Runtime determines output shape per inference step

## Failure Conditions

Model loading fails if:

-   Input or output tensor counts mismatch
-   Shapes mismatch
-   DataKind is undefined
-   Dynamic dimensions remain unresolved
-   Scalar or vector metadata is inconsistent

This strict validation guarantees deterministic inference behavior.

# 4. Final Runtime Model

After successful merge:

-   The pek::Model is fully resolved
-   All tensor dimensions are known
-   Data formats are fixed
-   Quantization parameters are fixed
-   Dynamic output behavior is defined

This object becomes immutable runtime configuration.

# 5. Architectural Properties

The model system provides:

-   Backend-agnostic runtime representation
-   Strict validation between model file and JSON metadata
-   Deterministic tensor layout
-   Explicit data format handling
-   Support for static and dynamic output shapes

This architecture enables:

-   Pluggable inference engines
-   Runtime model configuration without recompilation
-   Clean OpChain integration
-   Deterministic and validated inference execution
