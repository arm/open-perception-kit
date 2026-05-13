---
sidebar_position: 7
sidebar_label: Known limitations
---

# Known Limitations

This page captures the most important current limitations and architectural friction points in Perception Experience Kit.

## Scope and application boundary

- The core project boundary should start at model integration and structured
  `Perception` results, then end at publishing those results through a stable
  contract.
- `peksink` is useful for demos, but it currently mixes WebRTC delivery, HTTP
  serving, static UI hosting, control WebSocket handling, model registry state,
  and pipeline state control in one GStreamer element.
- Browser UI hosting should move out of `peksink`. A separate application or
  container should serve the UI endpoint, while the runtime publishes media and
  perception results.
- `pekcomm` can publish serialized `Perception` to file/stdout, but there is no
  standard, versioned application endpoint for perception results yet.

## peksink security and lifecycle

- `peksink` control and model-info endpoints are not ready to be treated as a
  secure product API. Authentication, authorization, input validation, and safe
  default network exposure need a dedicated review.
- `peksink` properties, request-pad behavior, thread startup/shutdown, and
  teardown ordering need more lifecycle testing before the element is treated as
  production-ready.

## Perception contracts and postprocessing

- New `Perception` result structures are still defined in C++ and usually
  require edits across the common model, serializer, parser, and visualization
  code.
- Users should be able to define perception structures schematically and publish
  those schema-defined results through the standard result endpoint.
- Custom postprocessing is still centered on C++ parsers registered in
  `GenericPostprocessOp`, a Python postprocessing path is not available yet.
- `pekosd` rendering is still hardcoded around known `contentType` values, so it
  is best treated as a debugging overlay rather than the long-term application
  visualization layer.

## Runtime failure handling

- Several runtime paths still use `assert()` or `pek_abort()` during setup,
  tensor handling, parser validation, and per-frame execution.
- Parser and tensor validation should return explicit errors instead of relying
  on asserts that crash debug builds and disappear from release builds.
- GStreamer elements should report proper element errors and fail the affected
  chain or buffer gracefully instead of aborting the whole process.

## Media support

- Video processing currently assumes linear, tightly packed BGRA frames in the
  main runtime elements.
- Padded stride, multi-planar formats, DMABUF, and zero-copy paths are not
  handled consistently yet.
- `peksink` can transport audio, but audio inference is not integrated.

## Configuration and OpChain contracts

- JSON configuration is central to models, OpChains, and pipelines, but the
  schemas, field semantics, and validation rules are not documented or tested
  deeply enough.
- Model descriptors and OpChains need schema validation tests before users rely
  on them as stable extension contracts.
- Descriptor versioning and migration rules are not defined, so released
  OpChains and model descriptors may break as the framework evolves.
- OpChain execution is ordered and supports grouped loops, but richer scheduling
  such as startup-only configuration stages is not represented cleanly yet.

## Models, backends, and platforms

- Model artifacts are checked into `config/models/`, they should move to a
  separate download/cache flow with manifests, checksums, and license metadata.
- ONNX Runtime and HailoRT are the main working backends. ExecuTorch is still
  incomplete, MNN is only planned, and RKNN/Orion6 is not a supported platform
  yet.
- Model performance and accuracy baselines are not published consistently,
  especially across CPU and accelerator variants.

## Performance and observability

- There are no clear performance goals for latency, FPS, CPU use, NPU use, or
  memory consumption.
- Runtime timing exists through `PerformanceTracer` and `pekperformance`, but
  measurement checkpoints are not yet a clear user-facing contract.
- Memory consumption is not measured by the built-in performance path.
- CPU and accelerator performance are not reported in a comparable way across
  supported models and platforms.

## Testing and quality

- Existing tests cover selected helpers, but coverage is still thin for parser
  behavior, known inference outputs, JSON/schema validation, and GStreamer
  element behavior.
- Integration tests should exercise existing postprocessors against known model
  outputs and expected `Perception` results.
- The repository has formatting and contribution notes, but no detailed coding
  guideline for C++, GStreamer, error handling, ownership, and testing style.
- Coding style and implementation quality are still inconsistent across older
  and newer areas of the tree.

## Packaging and deployment

- The development and deployment flow still assumes containers, source-tree
  layout, and many hardcoded `/work` paths.
- Host-side or non-container development is therefore harder than it should be.
- Binary distribution of the runtime, model descriptors, and OpChains is not
  ready with the current repository structure.
- Containers are built by users from the repository today; release-ready binary
  artifacts and deterministic container images are still needed.
- The DevOps flow should be simplified and made more deterministic before the
  project is treated as a product distribution.

Until these limitations are addressed, treat `model.json`, `opchain.json`,
`Perception`, and the existing GStreamer elements as the main experimental
integration surface. Treat `peksink`, `pekcomm` and the browser UI as a convenient demo
path, not as the final application boundary.
