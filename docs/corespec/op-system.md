# Op System
## Modular Operation Framework and Execution Model

The Op system is the core modular execution framework used by the `ampinfer`
GStreamer element.

It defines how processing units (Ops) are implemented, composed into OpChains,
dynamically loaded from shared libraries, and executed inside a GStreamer
pipeline.

The architecture separates:

- Operation lifecycle management
- Inference tensor access
- Postprocessing logic
- Chain construction and execution
- Dynamic module loading

---

# Architecture Overview

At runtime:

1. A JSON descriptor defines an OpChain.
2. Required Op implementations are loaded from shared libraries.
3. Op instances are created and configured.
4. The chain is bound to allow inter-Op coordination.
5. The OpChain is executed inside the `ampinfer` GStreamer element.
6. Each Op processes the shared OpChainContext sequentially.

Ops are small, composable processing units.
OpChains are ordered collections of Ops executed as a single processing pipeline.

---

# Op
## Base Class for All Operation Units

`amp::Op`

Op is the abstract base class for all processing units within an OpChain.
Each Op participates in a defined lifecycle consisting of configuration,
binding, and execution phases.

### Lifecycle Phases

- `configure(attributes)` initializes the Op using configuration parameters.
- `bind(index, ops)` allows the Op to inspect or connect to other Ops in the chain.
- `process(opChainContext)` performs the runtime work for a single execution step.

### Loop Control

- `isLoopHead()` allows an Op to declare itself as the head of a loop.
- The execution engine may iterate over a subchain when a loop head requests it.
- Loop heads and loop members must belong to the same group.

### Type-Safe Casting

Ops may expose optional capabilities.
The `as<T>()` helpers provide safe downcasting to derived interfaces.

This enables querying whether an Op supports inference or postprocessing.

---

# Inference and Postprocessing Interfaces

## OpInterfaceInference

`struct OpInterfaceInference`

Interface for Ops that provide tensor input and output access for inference execution.

Implementations expose:

- The associated model definition
- Raw memory addresses for tensor buffers

### Responsibilities

- Provide access to the model metadata.
- Provide raw tensor memory addresses by index.
- Ensure tensor memory remains valid during inference execution.

This interface allows the inference controller to operate independently
of the concrete Op implementation.

---

## OpInterfacePostprocessor

`struct OpInterfacePostprocessor`

Interface for Ops that perform inference postprocessing.

Implementations provide a postprocessor identifier used to associate
inference results with the correct parsing logic.

### Responsibilities

- Provide a stable postprocessor identifier.
- Execute domain-specific interpretation of inference outputs.

---

# OpChain

`amp::OpChain`

An OpChain is an ordered list of Ops constructed and executed as a unit.

Chains can be created:

- From an in-memory `OpChainDescriptor`
- From a JSON file (`setupFromFile`)

After construction:

- `bind()` is called to allow Ops to resolve dependencies.
- `execute(opChainContext)` runs the chain sequentially.

OpChain execution is integrated into the `ampinfer` GStreamer element
and processes media-driven workloads such as video inference pipelines.

---

# OpChainContext

`amp::OpChainContext`

OpChainContext is the transient execution context passed through the chain.

It provides shared runtime state for a single execution step and enables
communication between Ops.

Persistent results must be written to the Perception object rather than
stored in the context.

---

# OpChainDescriptor

`amp::OpChainDescriptor`

OpChainDescriptor is the declarative representation of an OpChain.

It contains the ordered list of Op definitions loaded from JSON.

Each Op entry specifies:

- `id` identifying the Op type
- optional `group` selecting the shared library
- `attributes` used for configuration

This enables runtime composition without recompilation.

---

# Dynamic Loading and OpRef

`amp::OpRef`

OpRef manages dynamic loading and lifetime of Op instances.

At runtime it:

- Loads a shared library (`.so`)
- Resolves factory functions
- Instantiates the Op
- Owns and destroys the instance safely

OpChain stores OpRefs for ownership and raw Op pointers for fast execution.

---

# Grouping and Shared Libraries

Ops are grouped by functionality into named groups.
Groups are compiled into shared libraries.

The OpChain JSON selects the group, and the system instantiates Ops
directly from the corresponding module at runtime.

Current standard Op libraries:

- `amp-std-ops.so`
- `amp-hailort-ops.so`
- `amp-onnx-ops.so`
- `amp-executorch-ops.so`

This modular structure:

- Isolates backend-specific dependencies
- Keeps the core engine runtime-agnostic
- Enables extending the system with new Op sets
- Avoids recompilation when adding functionality

*/