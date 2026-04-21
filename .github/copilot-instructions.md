# AMP Development Forge – Copilot Instructions

## Big picture
- The active runtime code lives under `development/`.
- GStreamer elements live in `development/elements/` and are built by `development/meson.build`.
- Runtime configuration lives under `config/`:
   - `config/models/` for model descriptors and model-local OpChains
   - `config/opchains/` for reusable multi-stage OpChains
   - `config/pipelines/` for top-level pipeline presets used by `amp-menu`
- `ampinfer` loads an OpChain JSON file, executes Ops from `ops-std/`, `ops-onnx/`, `ops-hailort/`, and optionally `ops-executorch/`, then writes results into `PerceptionMeta`.
- `amposd` and `ampperformance` read `PerceptionMeta`; overlay text is carried in `Perception.perfdata`.
- `ampsink` is a `GstBin` that owns the WebRTC, HTTP, and WebSocket control stack. Its default static content comes from `web/content`.

## Core code locations
- Shared perception and media types: `development/common/amp/`
- Op system and execution context: `development/common/op/`
- GStreamer metadata helpers: `development/common/gst/`
- Performance tracing: `development/common/PerformanceTracer.*`
- Pipeline launcher: `development/amp-menu/`
- Unit tests: `development/tests/`

## Current runtime facts
- Video-processing elements currently assume `BGRA` caps. Preserve that unless the task explicitly changes the pipeline contract.
- `PerceptionMeta` is the current buffer metadata type. Do not refer to `PerceptionContextMeta` in new docs or code comments.
- OpChain loop execution is driven by `loopId`, not by a named loop-group field.
- `InferenceControllerOp` populates `OpChainContext::inferenceImageCrops` and `inferenceImageCropUuids`.
- `ampinfer` exposes `opchain-path`, `active`, `format`, and `infer-id` properties.
- `ampperformance` writes formatted metrics into `Perception.perfdata`; it does not render the overlay itself.
- `amposd` renders Perception layers and performance text with Cairo.
- `ampsink` currently starts:
   - a WebRTC signaling WebSocket
   - a control WebSocket
   - an embedded HTTP server

## Build and test workflow
- Prefer working inside the Dev Container or containerized environment. The project assumes container-managed dependencies.
- Primary build script: `scripts/build-elements.sh`
   - debug: `./scripts/build-elements.sh debug [true|false]`
   - release: `./scripts/build-elements.sh release [true|false]`
   - ExecuTorch debug: `./scripts/build-elements.sh debug_with_executorch`
   - clean: `./scripts/build-elements.sh clean`
- The main build directory is `development/build`.
- `amp-menu` is built under `development/build/meson-out/amp-menu` and copied to `/work/tools/amp-menu` by the build script.
- Tests are Meson/GTest based from `development/tests/`. Build them with the `tests` option and run them with `meson test -C /work/development/build --print-errorlogs`.

## Repository-specific conventions
- Keep documentation aligned with `config/` and `development/`; avoid old `etc/` paths.
- When documenting models or OpChains, prefer actual checked-in examples from `config/models/*` and `config/opchains/*`.
- Op plugins must expose `amp_create_op_instance` and `amp_delete_op_instance` in their plugin entry files.
- Avoid inventing features that are not present in `development/`. If uncertain, verify the exact property names, metadata names, and field names in code first.
- Prefer the default extension surfaces before changing core runtime code:
   - `config/models/`
   - `config/opchains/`
   - `config/pipelines/`
   - `development/ops-std/postproc/`
- Preserve existing naming used by the codebase:
   - `PerceptionMeta`
   - `loopId`
   - `inferenceImageCrops`
   - `infer-id`
   - `perfdata`

## Docs maintenance guidance
- Architecture and element docs live under `docs/public/`.
- Keep docs grounded in the implementation under `development/`, not in historical naming.
- If you update paths or behavior in docs, verify them against the current code and `config/` layout.

## Agent-friendly entry points
- Repository-wide guidance for coding agents lives in `AGENTS.md`.
- Task-oriented agent playbooks live in `skills.md`.
- When extending the repository, start from these docs depending on the task:
   - `docs/public/how-to/deep-dives/engineering.md`
   - `docs/public/how-to/deep-dives/structural-basics.md`
   - `docs/public/how-to/deep-dives/runtime.md`
   - `docs/public/how-to/deep-dives/bring-your-model.md`
   - `docs/public/how-to/deep-dives/custom-postprocessing.md`
   - `docs/public/how-to/deep-dives/known-limitations.md`

## Extension heuristics for agents
- If the user wants a new runnable demo or exercise, start in `config/pipelines/`.
- If the user wants to onboard a new model, start in `config/models/` and `config/opchains/`.
- If the model runs but the outputs are not understood yet, start in `development/ops-std/postproc/`.
- If a new result must be visualized, inspect `development/elements/amposd/amposd.cpp` after the parser path is clear.
- Do not change `ampinfer`, `ampsink`, or shared runtime code first unless the task clearly requires deeper runtime work.

## Tooling notes
- multiple formatting rules are enforced in CI and by pre-commit hooks.
- If needed for clangd, symlink `compile_commands.json` from the build directory to the repository root after the first successful Meson configure/build.
