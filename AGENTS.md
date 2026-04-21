# AGENTS.md

This file is the starting point for coding agents working in AMP Development Forge.

The repository already has strong checked-in examples. Agents should prefer extending those examples and the documented default extension surfaces instead of inventing new patterns.

## Start here

Read these first before making substantial changes:

- [Engineering starting point](docs/public/how-to/deep-dives/engineering.md)
- [Structural basics](docs/public/how-to/deep-dives/structural-basics.md)
- [Runtime basics](docs/public/how-to/deep-dives/runtime.md)
- [Bring your model](docs/public/how-to/deep-dives/bring-your-model.md)
- [Custom postprocessing](docs/public/how-to/deep-dives/custom-postprocessing.md)
- [Known limitations](docs/public/how-to/deep-dives/known-limitations.md)

For implementation detail and background, continue with:

- [Architectural overview](docs/public/arch/architectural-overview.md)
- [Op system](docs/public/arch/op-system.md)
- [Perception](docs/public/arch/perception.md)
- [ampinfer](docs/public/arch/elements/ampinfer.md)
- [amposd](docs/public/arch/elements/amposd.md)
- [ampsink](docs/public/arch/elements/ampsink.md)

## Default extension surfaces

Agents should treat these as the default, user-facing extension points:

- `config/models/` for model descriptors and model-local opchains
- `config/opchains/` for reusable multi-stage inference chains
- `config/pipelines/` for top-level runnable presets used by `amp-menu`
- `development/ops-std/postproc/` for new tensor parsers

Do not start by changing core runtime code unless the task clearly requires it.

## Task routing

### Add or modify a runnable pipeline
Start in:

- `config/pipelines/`
- `docs/public/how-to/quick-guides/exercise.md` for a concrete walkthrough.

### Bring a new model into the system
Start in:

- `config/models/<your-model>/`
- `config/opchains/` or model-local `opchain.json`

Read first:

- [Exercise quick guide](docs/public/how-to/quick-guides/exercise.md)
- [Bring your model](docs/public/how-to/deep-dives/bring-your-model.md)

Useful checked-in examples:

- `config/models/cam-contact/`
- `config/models/yolov11/`

### Add custom postprocessing
Start in:

- `development/ops-std/postproc/`
- `development/ops-std/GenericPostprocessOp.cpp`
- `development/ops-std/meson.build`

Read first:

- [Custom postprocessing](docs/public/how-to/deep-dives/custom-postprocessing.md)

Useful checked-in examples:

- `development/ops-std/postproc/CameraContactParser.cpp`
- `development/ops-std/postproc/YoloParser.cpp`
- `development/ops-std/postproc/ImageNetClassificationParser.cpp`

### Add a new structured runtime result
Start in:

- `development/common/amp/Perception.h`
- `development/common/amp/PerceptionSerializer.h`
- `development/common/amp/PerceptionSerializer.cpp`

Then continue into parser and visualization code only if needed.

### Add or modify overlay rendering
Start in:

- `development/elements/amposd/amposd.cpp`

Only do this after the `Perception` structure and parser output are clear.

### Update docs
Ground doc changes in checked-in code and config.

Prefer these entry points:

- `docs/public/index.md`
- `docs/public/how-to/deep-dives/engineering.md`
- `docs/public/how-to/deep-dives/bring-your-model.md`
- `docs/public/how-to/deep-dives/custom-postprocessing.md`

## Important repository facts

- The active runtime code lives under `development/`.
- Video-processing elements currently assume `BGRA` caps unless the task explicitly changes the contract.
- `PerceptionMeta` is the current metadata type.
- OpChain loop execution is driven by `loopId`.
- `ampperformance` writes to `Perception.perfdata`; `amposd` renders it.
- `ampsink` currently owns the WebRTC, HTTP, and control WebSocket stack.

## Build and validation

Prefer the container workflow.

Primary build script:

- `./scripts/build-elements.sh debug [true|false]`
- `./scripts/build-elements.sh release [true|false]`

Build directory:

- `development/build`

Tests:

- build with tests enabled
- `./scripts/build-elements.sh debug true`

If you change docs, at minimum verify that paths, file names, and checked-in examples still exist.

## Agent guardrails

- Reuse checked-in patterns before inventing new ones.
- Keep docs aligned with `config/` and `development/`.
- If a task is actually blocked by current architecture, say so and cross-check [Known limitations](docs/public/how-to/deep-dives/known-limitations.md).
