---
sidebar_position: 11
sidebar_label: Op System
---

# Op System
## Modular Operation Framework and Execution Model

The **Op system** is the modular execution framework that powers the
`pekinfer` GStreamer element.

It defines how processing units (**Ops**) are implemented, dynamically loaded,
assembled into micropipelines (**OpChains**), and executed within a live
GStreamer pipeline.

Conceptually:

- **Ops** are small, single-responsibility processing units.
- **OpChains** are ordered micropipelines built from Ops.
- **pekinfer** is the GStreamer element that hosts and executes an OpChain
  for each media-driven execution step (e.g., per video frame).

# Architectural Positioning

The system operates at two distinct levels:

### 1. GStreamer Level

`pekinfer` is a GStreamer element inserted into a standard media pipeline:

v4l2src → videoconvert → pekinfer → autovideosink

`pekinfer` receives buffers (e.g., video frames) from the pipeline and
triggers inference processing.

### 2. OpChain Level (micropipeline)

Inside `pekinfer`, an **OpChain** executes as a self-contained micropipeline:

```
[InferenceController]
  ↓
[GenericImagePreprocess]
  ↓
[Inference]
  ↓
[GenericPostprocess]
```

This internal pipeline is fully decoupled from GStreamer mechanics.
It operates on an `OpChainContext` and domain-specific data structures.

In summary:

- **OpChains are built from Ops**
- **OpChains are executed inside the `pekinfer` GStreamer element**
- GStreamer handles media scheduling
- OpChains handle inference logic

---

# Runtime Execution Model

At runtime:

1. A JSON descriptor defines an OpChain.
2. Required Op implementations are loaded from shared libraries.
3. Op instances are created and configured.
4. The chain is bound to allow inter-Op coordination.
5. `pekinfer` invokes `OpChain::execute()` per processing step.
6. Each Op processes the shared `OpChainContext` sequentially.

The execution is strictly ordered and deterministic.

---

# Op
## Base Processing Unit

`pek::Op`

`Op` is the abstract base class for all processing units within an OpChain.

Each Op follows a defined lifecycle:

### Lifecycle Phases

- `configure(attributes)`  
  Initializes the Op using JSON-provided configuration.

- `bind(index, ops)`  
  Allows the Op to inspect other Ops in the chain and establish dependencies.

- `process(opChainContext)`  
  Executes runtime logic for a single step.

Ops are intentionally small and composable.
They encapsulate a single responsibility (preprocess, inference, postprocess, control).

---

# Inference and Postprocessing Interfaces

## OpInterfaceInference

`struct OpInterfaceInference`

Defines the contract for Ops that expose tensor memory and model metadata
to the inference execution layer.

Responsibilities:

- Expose model definition metadata
- Provide raw input/output tensor memory addresses
- Guarantee tensor memory validity during execution

This interface allows inference backends to remain independent from
concrete Op implementations.

---

## OpInterfacePostprocessor

`struct OpInterfacePostprocessor`

Defines the contract for Ops performing inference output interpretation.

Responsibilities:

- Provide a stable postprocessor identifier
- Implement domain-specific parsing of inference outputs

This enables separation between model execution and result interpretation.

---

# OpChain

`pek::OpChain`

An OpChain is an ordered collection of Ops forming a micropipeline.

Construction methods:

- From an in-memory `OpChainDescriptor`
- From a JSON file (`setupFromFile`)

After construction:

- `bind()` resolves inter-Op dependencies
- `execute(opChainContext)` runs the chain sequentially

OpChain execution is invoked by the `pekinfer` GStreamer element
for each media-driven execution event.

---

# OpChainContext

`pek::OpChainContext`

Transient execution context passed through the OpChain.

It:

- Provides shared runtime state for a single execution step
- Enables communication between Ops
- Carries tensor references and intermediate data

Persistent results must be written into the Perception object
and not stored inside the context.

---

# OpChainDescriptor

`pek::OpChainDescriptor`

Declarative representation of an OpChain loaded from JSON.

Each Op entry specifies:

- `id` — library/op identifier used for dynamic loading (for example `pek-std-ops/InferenceController`)
- `group` — optional string copied onto the runtime `Op`
- `loopId` — optional repeated-execution group identifier
- `attributes` — configuration parameters

This enables runtime composition without recompilation.

---

# Dynamic Loading and OpRef

`pek::OpRef`

Responsible for dynamic loading and lifetime management of Ops.

At runtime it:

- Loads a shared library (`.so`)
- Resolves factory functions
- Instantiates the Op
- Owns and safely destroys the instance

`OpChain` stores:

- `OpRef` objects for ownership
- Raw `Op*` pointers for fast execution

---

# Grouping and Shared Libraries

Ops are grouped by functionality into shared libraries.
Each group encapsulates a specific backend or functional domain.

This design:

- Isolates backend-specific dependencies
- Keeps the core runtime backend-agnostic
- Enables extending the system without recompilation
- Supports pluggable inference backends

---

# Currently Implemented Ops

## pek-std-ops.so

General-purpose and orchestration Ops:

- **InferenceController**  
  Creates image crops for cascaded model inputs and manages the loop when multiple inferences are present.

- **GenericImagePreprocess**  
  Performs generic image preprocessing (resize, normalization, layout conversion)
  to prepare tensors for inference.

- **GenericPostprocess**  
  Performs model-agnostic output handling and forwards results
  to Perception structure.

---

## pek-hailort-ops.so

Hailo backend-specific Ops:

- **Inference**  
  Executes inference using the HailoRT runtime and accelerator hardware.

---

## pek-onnx-ops.so

ONNX backend-specific Ops:

- **Inference**  
  Executes inference using ONNX Runtime.
