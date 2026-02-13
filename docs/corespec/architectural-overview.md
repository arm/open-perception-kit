# 🏗 Architectural Overview

The system is built around a modular **OpChain execution logic**.

At runtime:

- A GStreamer pipeline feeds frames into the system.
- Frames enter the OpChain execution environment.
- Ops process frames sequentially using a shared transient context.
- Inference operations are scheduled and executed via supported runtimes.
- Postprocessing attaches persistent results to a Perception object.
- Preprocessing is handled by configurable video and audio preprocessors.
- The Perception object travels downstream as the persistent result container.

The architecture cleanly separates:

- Transient execution state (OpChainContext)
- Persistent output state (Perception)
- Inference runtime abstraction
- Postprocessing logic
- Loop and execution control
- Runtime integration layer

---

# Op Modules and Dynamic Loading

Ops are grouped by functionality into dynamically loadable shared libraries (`.so` files).
This modular structure allows the system to remain extensible while keeping runtime
dependencies isolated by backend or feature domain.

---

## OpChain Construction

OpChains are defined declaratively using a JSON configuration file.

At startup:

- The system parses the JSON definition.
- The required shared libraries are loaded dynamically.
- Op instances are instantiated directly from the loaded `.so` modules.
- The OpChain is constructed in the order defined in the configuration.
- The `configure` and `bind` lifecycle phases are executed.
- The chain becomes ready for runtime execution.

This design enables flexible pipeline composition without recompilation.

---

## Standard Op Libraries

The system provides a standard set of Op libraries:

- `amp-std-ops.so`  
  Core processing Ops and general-purpose components.

- `amp-hailo-ops.so`  
  Hailo runtime–specific inference Ops.

- `amp-onnx-ops.so`  
  ONNX Runtime–based inference Ops.

- `amp-executorch-ops.so`  
  ExecuTorch backend integration Ops.

Additional Op libraries can be added without modifying the core execution engine,
provided they conform to the Op interface contract.

---

## Architectural Benefits

- Backend-specific logic is isolated.
- New inference runtimes can be integrated as separate modules.
- The core engine remains runtime-agnostic.
- Deployment artifacts remain modular.
- Feature sets can be enabled or disabled per deployment.

This modular loading mechanism is fundamental to the system’s extensibility model.

---

# 🧠 Inference Runtime Support

The framework supports multiple inference backends:

- ONNX Runtime
- Hailo Runtime
- ExecuTorch

The runtime abstraction layer allows:

- Easy integration of new models
- Backend-agnostic Op implementation
- Consistent tensor access patterns
- Clear lifecycle control

Model integration is intentionally simple and structured to reduce friction
when deploying new networks into existing pipelines.

---
