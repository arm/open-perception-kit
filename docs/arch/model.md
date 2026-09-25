---
sidebar_position: 6
sidebar_label: opk::Model
---

# opk::Model

The model system separates backend introspection from user-provided model
metadata. Inference backends load a model and expose discovered tensor metadata;
OPK merges that information with the JSON model descriptor to produce a validated,
engine-agnostic `opk::Model`.

The merge has two inputs:

1. Metadata extracted by the selected backend, such as tensor names, shapes,
   element types, and quantization parameters.
2. Metadata from `model.json`, such as model identity, content type, data layout,
   normalization, and static shape overrides.

If both sources define the same property, they must agree. Otherwise model loading
fails.

## Runtime Representation

`opk::Model` is the canonical runtime representation used by inference Ops. It
contains resolved input and output tensor definitions, including:

- tensor names and shapes
- tensor element type (`Tdt`)
- tensor layout or semantic kind (`DataKind`)
- quantization parameters
- normalization parameters for inputs
- model name and content type
- static or dynamic output behavior

Once constructed, the object is treated as resolved runtime configuration for the
OpChain.

## Descriptor Metadata

A model descriptor supplies the metadata that cannot always be recovered reliably
from the backend model file. A minimal image model descriptor looks like this:

```json
{
  "version": "1.0.0",
  "name": "yolo26n-320-int8",
  "contentType": "genericObject",
  "modelFile": "yolo26n_raspberry_onnx_optimized.onnx",
  "dynamicOutput": true,
  "inputTensors": [
    {
      "shape": [1, 3, 320, 320],
      "dataKind": "ImageRgbChw"
    }
  ]
}
```

Descriptors can define tensor shapes, data kinds, value types, normalization
values, and scalar/vector input metadata. Quantization values come from backend
model inspection rather than descriptor fields.

## Loading Lifecycle

Model loading follows a fixed sequence:

1. The backend loads the model file and extracts the metadata it supports.
2. OPK creates an initial `opk::Model` from backend data.
3. `opk::Model::applyModelFromDescriptor` merges descriptor metadata into the
   engine-derived model.
4. Validation resolves dynamic dimensions and checks descriptor/backend agreement.
5. The resolved `opk::Model` is passed to the inference Op.

## Validation Rules

Input tensor validation checks tensor count, data kind, shape compatibility,
dynamic dimension resolution, and scalar/vector consistency.

Output tensor validation depends on `dynamicOutput`:

- When `dynamicOutput` is false, output tensor count and shapes must match or be
  resolvable from backend dynamic shapes.
- When `dynamicOutput` is true, the descriptor must not define output tensors;
  the runtime resolves output shapes during execution.

Strict validation keeps tensor layout deterministic and prevents silent backend or
parser mismatches.
