---
sidebar_position: 12
sidebar_label: Op System
---

# Op System

The Op system is the modular execution framework used by `opkinfer`. It defines
how processing units are configured, loaded, assembled into OpChains, and run for
each media-driven processing step.

At a high level:

- An `Op` is a small processing unit with a defined lifecycle.
- An `OpChain` is an ordered micropipeline built from Ops.
- `opkinfer` is the GStreamer element that hosts and executes an OpChain for each buffer or processing event.

## Architectural Position

The system has two layers. GStreamer handles media transport and scheduling,
while OpChains handle the OPK processing logic.

```text
v4l2src -> videoconvert -> opkinfer -> autovideosink
```

Inside `opkinfer`, a typical OpChain may look like this:

```text
InferenceController
  -> GenericImagePreprocess
  -> Inference
  -> GenericPostprocess
```

The internal chain is decoupled from GStreamer mechanics. It operates on an
`OpChainContext` and domain-specific data structures, so the same processing
model can be reused outside a live GStreamer pipeline.

## Runtime Flow

At runtime:

1. A JSON descriptor defines the OpChain.
2. Required Op implementations are loaded from shared libraries.
3. Op instances are created and configured.
4. The chain is bound so Ops can resolve inter-Op dependencies.
5. `opkinfer` invokes `OpChain::execute()` for each processing step.
6. Each Op processes the shared `OpChainContext` sequentially.

Execution is ordered and deterministic for a given descriptor and input state.

## Core Contracts

### Op

`opk::Op` is the abstract base class for processing units in an OpChain. Each Op
uses the same lifecycle:

- `configure(attributes)` initializes the Op from JSON configuration.
- `bind(index, ops)` lets the Op inspect the chain and establish dependencies.
- `process(opChainContext)` runs the Op for a single execution step.

Ops should stay focused on one responsibility, such as preprocessing, inference,
postprocessing, or orchestration.

### OpChain

`opk::OpChain` is an ordered collection of Ops. It can be constructed from an
in-memory `OpChainDescriptor` or from a JSON file. After construction,
`bind()` resolves dependencies and `execute(opChainContext)` runs the chain in
order.

### OpChainContext

`opk::OpChainContext` is transient state for one execution step. It carries the
shared runtime data needed by Ops, including tensor references and intermediate
values. Persistent output is appended to the `FrameResults` instance referenced
by the context rather than stored as transient context state. See
[OpChain Context](op-chain-context.md).

### OpChainDescriptor

`opk::OpChainDescriptor` is the declarative representation loaded from JSON. Each
Op entry can provide:

- `id`: library and Op identifier used for dynamic loading.
- `loopId`: optional repeated-execution group identifier.
- `attributes`: Op-specific configuration.

The exact `<library>/Inference` operation name is the OpChain v1 inference
extension contract. Its only attribute is the required filesystem path
`modelDescriptor`. Relative paths resolve from the OpChain descriptor directory;
absolute paths are used unchanged. The loader preserves path components rather
than lexically normalizing them, so filesystem symlink and parent-traversal
semantics remain intact. Differently named Ops remain custom and keep open
attributes.

This keeps composition and model changes in configuration instead of requiring a
rebuild. A non-zero `loopId` forms one contiguous group of at least two Ops and
may not reappear later in the chain. The scheduler executes the group's first Op
once, then repeats the remaining Ops until one of them breaks the loop. A built-in
stage is either entirely unlooped or assigns the same non-zero `loopId` to its
controller, preprocess, inference, optional custom Ops, and final postprocess. In
the looped form, InferenceController starts the group. A non-empty controller
`contentType` requires the looped form.

The optional `opk-python-ops/PythonScript` operation loads a Python module once
and calls `process(env, tensors, context)` on each execution. The call-scoped
context provides producer identity for payloads created by the script. Absolute
`script` and `pythonPaths` values are used unchanged. Relative values resolve
from the directory containing the inference operation's `modelDescriptor`. An
OpChain whose model descriptors occupy multiple directories must use absolute
Python paths. It is a generic hook: before inference it receives an empty tensor
tuple, while after inference it receives the latest output tensors as read-only
NumPy views. The views are zero-copy and valid only for the duration of the call.
The operation is supported by native pipelines in the official OPK containers
and by extracted OPK binary releases on Debian Trixie. Containers use their
locked virtual environment; binary releases use the system CPython interpreter
and package-relative locked Python dependencies.

## Inference and Postprocessing Interfaces

Some Ops expose narrower contracts used by inference and postprocessing code:

- `OpInterfaceInference` exposes tensor memory and model metadata to inference backends.
- `OpInterfacePostprocessor` reports the semantic content types produced by
  domain-specific output parsing.

These interfaces keep backend execution and result interpretation separate from
concrete Op implementations.

A new inference library that uses `<library>/Inference` must implement
`OpInterfaceInference` and consume the shared `modelDescriptor` contract. The
descriptor validator intentionally does not enumerate backend libraries or load
plugins; missing factories, interface mismatches, artifacts, and backend
compatibility are runtime errors.

## Loading and Extension

`opk::OpRef` owns dynamically loaded Ops. It loads the shared library, resolves
factory functions, creates the Op instance, and destroys it safely when the chain
is torn down.

Ops are grouped into shared libraries by backend or functional domain. This keeps
backend dependencies isolated, keeps the core runtime backend-agnostic, and lets
new Ops be added without recompiling the core framework.

Release packages install Op modules beside OPK's private libraries in
`lib/opk`; the private library RUNPATH lets the existing bare module names
resolve without `LD_LIBRARY_PATH`. Both architecture packages contain the
standard and ONNX operation modules plus the experimental ExecuTorch operation
module.

Checked-in Op implementations live under `development/ops-*`, including standard
orchestration Ops and backend-specific inference Ops. Treat that tree as the
source of truth for the current implementation set.
