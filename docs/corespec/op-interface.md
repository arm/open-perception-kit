# Op Interfaces
## Contracts for Inference, Postprocessing, and Execution

This section defines the core interfaces that structure Op behavior within an OpChain.
These interfaces separate responsibilities between model execution, postprocessing,
and general Op lifecycle management.

---

## OpInterfaceInference

`struct OpInterfaceInference`

Interface for Ops that provide tensor input and output access for inference execution.
Implementations expose the model description and the memory locations of tensor buffers.
This interface enables the inference system to retrieve model metadata and directly
access tensor data owned or managed by the Op.

### Responsibilities

- Provide access to the associated model definition.
- Provide raw memory addresses for tensor buffers by index.
- Ensure tensor memory remains valid during inference execution.

---

## OpInterfacePostprocessor

`struct OpInterfacePostprocessor`

Interface for Ops that perform inference postprocessing.
Implementations identify themselves via a postprocessor ID.
This ID is used to associate inference results with the correct postprocessing logic.

### Responsibilities

- Provide a stable identifier for the postprocessor implementation.
- Execute domain-specific interpretation of inference outputs.

---

# Op
## Base Class for All Operation Units

`struct Op`

Op is the abstract base class for all processing units within an OpChain.
Each Op participates in a defined lifecycle consisting of configuration,
binding, and execution phases.

### Lifecycle Phases

- `configure` is called immediately after instance creation and allows the Op
  to initialize itself using provided attributes.
- `bind` is called when the OpChain is constructed and allows the Op to inspect
  or connect to other Ops in the chain.
- `process` is called during execution and performs the actual runtime work
  using the provided OpChainContext.

### Loop Control

- `isLoopHead` allows an Op to declare itself as the head of a loop.
- A loop head can instruct the execution system to iterate over a subchain
  multiple times.
- The loop head and loop members must belong to the same group.

### Type-Safe Casting

- The `as<T>()` helpers provide safe downcasting to derived interfaces.
- These helpers simplify querying optional capabilities such as inference
  or postprocessing support.

