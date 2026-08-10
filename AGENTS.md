# AGENTS.md

This file is the starting point for coding agents working in Perception Experience Kit.

Prefer checked-in examples and documented extension surfaces over inventing new patterns.

## Workflow

For anything beyond a tiny local edit:

1. read the relevant checked-in docs and examples first
2. identify the intended extension surface before editing
3. make a short implementation and verification plan
4. implement only the changes the task needs
5. report what was verified and what was not

## Start here

Read these first before making substantial changes:

- [Contribution rules](.github/CONTRIBUTING.md)
- [Structural basics](docs/public/concepts/structural-basics.md)
- [Runtime basics](docs/public/concepts/runtime-basics.md)
- [Bring your model](docs/public/how-to/bring-your-model.md)
- [Custom postprocessing](docs/public/how-to/custom-postprocessing.md)
- [Known limitations](docs/arch/known-limitations.md)

For implementation detail and background, continue with:

- [Architectural overview](docs/arch/architectural-overview.md)
- [Op system](docs/arch/op-system.md)
- [Perception](docs/arch/perception.md)
- [pekinfer](docs/arch/elements/pekinfer.md)
- [pekosd](docs/arch/elements/pekosd.md)
- [peksink](docs/arch/elements/peksink.md)

## Default extension surfaces

- `config/models/` for model descriptors and model-local opchains
- `config/opchains/` for reusable multi-stage inference chains
- `config/pipelines/` for top-level runnable presets used by `pek-menu`
- `development/ops-std/postproc/` for new tensor parsers

Do not start by changing core runtime code unless the task clearly requires it.

## Task routing

### Prepare or troubleshoot a release

Use the repository-local `$opk-release` skill for every release task, including
version selection, changelog preparation, release PRs, and release CI failures.
Read and follow [its instructions](.agents/skills/opk-release/SKILL.md) before
taking release actions.

### Add or modify a runnable pipeline
Start in:

- `config/pipelines/`
- `docs/public/how-to/media-input.md` for a concrete walkthrough.

### Bring a new model into the system
Start in:

- `config/models/<your-model>/`
- `config/opchains/` or model-local `opchain.json`

Read first:

- [Media input guide](docs/public/how-to/media-input.md)
- [Bring your model](docs/public/how-to/bring-your-model.md)

Useful checked-in examples:

- `config/models/cam-contact/`
- `config/models/yolov11/`

### Add custom postprocessing
Start in:

- `development/ops-std/postproc/`
- `development/ops-std/GenericPostprocessOp.cpp`
- `development/ops-std/meson.build`

Read first:

- [Custom postprocessing](docs/public/how-to/custom-postprocessing.md)

Useful checked-in examples:

- `development/ops-std/postproc/CameraContactParser.cpp`
- `development/ops-std/postproc/YoloParser.cpp`
- `development/ops-std/postproc/ImageNetClassificationParser.cpp`

### Add a new structured runtime result
Start in:

- `development/common/pek/Perception.h`
- `development/common/pek/PerceptionSerializer.h`
- `development/common/pek/PerceptionSerializer.cpp`

Then continue into parser and visualization code only if needed.

### Add or modify overlay rendering
Start in:

- `development/elements/pekosd/pekosd.cpp`

Only do this after the `Perception` structure and parser output are clear.

### Add or modify an app under `apps/`
Start in:

- `apps/<app-name>/`
- `metadata/AGENTS.md` if the app consumes AMP JSON metadata

Prefer simple, self-contained app code unless there is already an established shared pattern to reuse.

For browser apps that consume metadata:

- route on `layer.contentType` and `detection.type`, not layer order
- define the coordinate-space policy explicitly before mapping detections into UI or gameplay
- use `VideoFrame` dimensions when available and treat other dimension recovery as heuristic
- treat left/right assignment and mirrored-camera behavior as app-level logic
- add smoothing and dropout handling before using metadata for realtime control
- document whether control uses rectangle center, edge, or another anchor point
- keep app-specific assumptions in the app README so they do not become implicit contract

### Update docs
Ground doc changes in checked-in code and config.

- `docs/public/index.md`
- `docs/public/concepts/structural-basics.md`
- `docs/public/how-to/bring-your-model.md`
- `docs/public/how-to/custom-postprocessing.md`

## Important repository facts

- Branch and commit-message rules are documented in `.github/CONTRIBUTING.md`.
- The active runtime code lives under `development/`.
- Video-processing elements currently assume `BGRA` caps unless the task explicitly changes the contract.
- `PerceptionMeta` is the current metadata type.
- OpChain loop execution is driven by `loopId`.
- `pekperformance` writes to `Perception.perfdata`; `pekosd` renders it.
- `peksink` currently owns the WebRTC, HTTP, and control WebSocket stack.

## Build and validation

- `./scripts/build-elements.sh debug [true|false]`
- `./scripts/build-elements.sh release [true|false]`
- `./scripts/build-elements.sh clean`
- `./scripts/build-elements.sh debug true`
- `meson test -C /work/development/build --print-errorlogs`
- `./scripts/gen-doc.sh` to refresh generated docs, Doxygen output, and PlantUML images
- `./scripts/serve-docs-plain.sh`
- `./scripts/serve-docs.sh`

Incomplete verification step available:

- runtime changes: build with tests and run `meson test`
- config or pipeline changes: if `./tools/pek-menu` exists, use `./tools/pek-menu -p <pipeline-id-or-path>`
- docs or diagrams: run `./scripts/gen-doc.sh`

## Agent guardrails

- Reuse checked-in patterns before inventing new ones.
- Keep docs aligned with `config/` and `development/`.
- When replacing behavior, delete obsolete code, stale tests, old docs, and
  legacy entrypoints in the same change. Do not keep compatibility shims unless
  the current supported contract explicitly requires them.
- Prefer the container workflow.
- If a task is actually blocked by current architecture, say so and cross-check [Known limitations](docs/arch/known-limitations.md).
