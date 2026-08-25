# Perception Experience Kit – Copilot Instructions

Follow the repository root `AGENTS.md` first.

## Quick map
- Active runtime: `development/`
- Default extension surfaces:
  - `config/models/`
  - `config/opchains/`
  - `config/pipelines/`
  - `development/ops-std/postproc/`
- Shared types: `development/common/pek/`
- Perception SDK descriptor: `tools/perception/sdk.json`
- Perception schemas and generated SDKs: use the paths declared by the descriptor
- Op system: `development/common/op/`
- GStreamer metadata: `development/common/gst/`
- Launcher: `development/pek-menu/`
- Tests: `development/tests/`
- Docs: `docs/public/`

## Preserve current contracts
- Video-processing elements expect `BGRA` unless the task changes the contract.
- Runtime result metadata is `FrameResultsMeta`.
- OpChain loops use `loopId`.
- `pekinfer` executes OpChains and appends generated FrameResults payloads.
- `pekperformance` appends `PerformanceOverlayT` FrameResults payloads.
- `pekosd` renders supported FrameResults overlays.
- `pekcomm` publishes serialized FrameResults packets for file/stdout output.
- `peksink` owns the WebRTC, HTTP, and control WebSocket stack.

## Working style
- Reuse checked-in examples before inventing new patterns.
- Prefer `config/` or `development/ops-std/postproc/` before editing core runtime code.
- For non-trivial tasks: inspect docs/examples first, make a short plan, and verify with the most specific command available.
- State clearly what you verified and what you did not verify.

## Tests and Fixtures
- `tools/expkits-ci/tests/fixtures/` is test data.
- Files there may be intentionally broken or secret-like.
- Do not suggest fixing them unless the task is about fixtures or tests.

## Validation
- Build: `./scripts/build.sh debug [true|false]` or `./scripts/build.sh release [true|false]`
- Clean: `./scripts/build.sh clean`
- Tests: `./scripts/build.sh debug true` then `meson test -C /work/development/build --print-errorlogs`
- Pipeline dry-run: `./tools/pek-menu -p <pipeline-id-or-path>` if available
- Docs and diagrams: `./scripts/gen-doc.sh`
- Docs preview: `./scripts/serve-docs-plain.sh` or `./scripts/serve-docs.sh`
