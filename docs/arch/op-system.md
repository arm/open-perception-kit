---
sidebar_position: 12
sidebar_label: Op System
---

# Op System

The Op system is the modular execution framework used by `pekinfer`. It defines
how processing units are configured, loaded, assembled into OpChains, and run for
each media-driven processing step.

At a high level:

- An `Op` is a small processing unit with a defined lifecycle.
- An `OpChain` is an ordered micropipeline built from Ops.
- `pekinfer` is the GStreamer element that hosts and executes an OpChain for each buffer or processing event.

## Architectural Position

The system has two layers. GStreamer handles media transport and scheduling,
while OpChains handle the OPK processing logic.

```text
v4l2src -> videoconvert -> pekinfer -> autovideosink
```

Inside `pekinfer`, a typical OpChain may look like this:

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
5. `pekinfer` invokes `OpChain::execute()` for each processing step.
6. Each Op processes the shared `OpChainContext` sequentially.

Execution is ordered and deterministic for a given descriptor and input state.

## Core Contracts

### Op

`pek::Op` is the abstract base class for processing units in an OpChain. Each Op
uses the same lifecycle:

- `configure(attributes, setupContext)` initializes the Op from JSON
  configuration. Inference Ops resolve and materialize their model descriptors
  through the setup-scoped `OpSetupContext` before backend setup.
- `bind(index, ops)` lets the Op inspect the chain and establish dependencies.
- `process(opChainContext)` runs the Op for a single execution step.

Ops should stay focused on one responsibility, such as preprocessing, inference,
postprocessing, or orchestration.

### OpChain

`pek::OpChain` is an ordered collection of Ops. It can be constructed from an
in-memory `OpChainDescriptor` or from a JSON file. After construction,
`bind()` resolves dependencies and `execute(opChainContext)` runs the chain in
order.

### OpChainContext

`pek::OpChainContext` is transient state for one execution step. It carries the
shared runtime data needed by Ops, including tensor references and intermediate
values. Persistent output belongs in `Perception`, not in the context. See
[OpChain Context](op-chain-context.md).

### OpChainDescriptor

`pek::OpChainDescriptor` is the declarative representation loaded from JSON. Each
Op entry can provide:

- `id`: library and Op identifier used for dynamic loading.
- `group`: optional grouping label copied onto the runtime Op.
- `loopId`: optional repeated-execution group identifier.
- `attributes`: Op-specific configuration.

This keeps composition and model changes in configuration instead of requiring a
rebuild.

## Inference and Postprocessing Interfaces

Some Ops expose narrower contracts used by inference and postprocessing code:

- `OpInterfaceInference` exposes tensor memory and model metadata to inference backends.
- `OpInterfacePostprocessor` identifies and runs domain-specific output parsing.

These interfaces keep backend execution and result interpretation separate from
concrete Op implementations.

## Loading and Extension

`pek::OpRef` owns dynamically loaded Ops. It loads the shared library, resolves
factory functions, creates the Op instance, and destroys it safely when the chain
is torn down.

Ops are grouped into shared libraries by backend or functional domain. This keeps
backend dependencies isolated, keeps the core runtime backend-agnostic, and lets
new Ops be added without recompiling the core framework.

Checked-in Op implementations live under `development/ops-*`, including standard
orchestration Ops and backend-specific inference Ops. Treat that tree as the
source of truth for the current implementation set.
