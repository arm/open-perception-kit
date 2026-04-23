# AMP Development Forge – Copilot Instructions

Follow the repository root `AGENTS.md` first.

## Quick map
- Active runtime: `development/`
- Default extension surfaces:
  - `config/models/`
  - `config/opchains/`
  - `config/pipelines/`
  - `development/ops-std/postproc/`
- Shared types: `development/common/amp/`
- Op system: `development/common/op/`
- GStreamer metadata: `development/common/gst/`
- Launcher: `development/amp-menu/`
- Tests: `development/tests/`
- Docs: `docs/public/`

## Preserve current contracts
- Video-processing elements expect `BGRA` unless the task changes the contract.
- Buffer metadata is `PerceptionMeta`.
- OpChain loops use `loopId`.
- `ampinfer` executes OpChains and writes `PerceptionMeta`.
- `ampperformance` writes text into `Perception.perfdata`.
- `amposd` renders overlays.
- `ampsink` owns the WebRTC, HTTP, and control WebSocket stack.

## Working style
- Reuse checked-in examples before inventing new patterns.
- Prefer `config/` or `development/ops-std/postproc/` before editing core runtime code.
- For non-trivial tasks: inspect docs/examples first, make a short plan, and verify with the most specific command available.
- State clearly what you verified and what you did not verify.

## Validation
- Build: `./scripts/build-elements.sh debug [true|false]` or `./scripts/build-elements.sh release [true|false]`
- Clean: `./scripts/build-elements.sh clean`
- Tests: `./scripts/build-elements.sh debug true` then `meson test -C /work/development/build --print-errorlogs`
- Pipeline dry-run: `./tools/amp-menu -p <pipeline-id-or-path>` if available
- Docs and diagrams: `./scripts/gen-doc.sh`
- Docs preview: `./scripts/serve-docs-plain.sh` or `./scripts/serve-docs.sh`
