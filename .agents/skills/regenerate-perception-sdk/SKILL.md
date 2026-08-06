---
name: regenerate-perception-sdk
description: Regenerate and validate the canonical checked-in Perception C++, Python, and TypeScript SDK snapshot during implementation. Use when schemas, tools/perception/sdk.json, flowdata-sdk, formatting rules, generated build integrations, or generator behavior require updates under generated/perception or development/perception/meson.build; when the SDK drift check fails; or when generated SDK sources must be prepared for a normal source commit. Do not use this skill to create release ZIP bundles.
---

# Regenerate Perception SDK

Update the repository's generated SDK snapshot from authored inputs and leave a
reviewable, commit-ready source diff. Do not create release artifacts.

## Confirm the Workflow

Use this workflow when implementation requires new generated C++, Python, or TypeScript SDK
sources. The outputs are normal tracked repository files and must be committed
with the authored inputs that produced them.

Do not use `package` as a substitute for regeneration. Packaging consumes the
checked-in snapshot and never updates generated source files. Use
`$package-perception-sdk-release` only after the implementation snapshot is
committed and ready to release.

## Inspect Inputs

1. Read `tools/perception/sdk.json` and `schemas/perception/README.md`.
2. Inspect applicable `AGENTS.md` files and the authored input changes.
3. Preserve unrelated working-tree changes.
4. Confirm that `tools/flowdata-sdk` is an initialized Git checkout:

   ```bash
   git -C tools/flowdata-sdk rev-parse HEAD
   ```

5. If schemas changed, apply `$evolve-perception-schema` first to classify
   compatibility and choose the SDK version.

## Regenerate

Run from the repository root with the PEK development container running:

```bash
./scripts/perception-sdk.sh generate
```

The wrapper executes inside the configured PEK container. Start it with
`./scripts/quick_start.sh` when necessary, or set `PEK_CONTAINER` when using a
non-default container.

Generation atomically replaces:

- `generated/perception/`
- `development/perception/meson.build`

It also verifies raw FlowData manifests, applies project formatting and license
decoration, writes the Perception generation receipt, and validates the final
snapshot. Never edit these outputs by hand.

## Review the Generated Diff

Inspect authored inputs and generated outputs together:

```bash
git status --short -- \
  schemas/perception \
  tools/perception/sdk.json \
  tools/flowdata-sdk \
  generated/perception \
  development/perception/meson.build

git diff --stat -- \
  schemas/perception \
  tools/perception/sdk.json \
  tools/flowdata-sdk \
  generated/perception \
  development/perception/meson.build

git diff --check
```

Confirm that:

- generated payload identities and schema-set digests changed only as expected
- C++, Python, and TypeScript APIs describe the same payload set
- generated CMake, Meson, and Python bridge integrations remain present
- the generation manifest records the intended SDK and tool versions
- obsolete generated files disappeared when their authored inputs were removed

## Validate

Run the canonical drift and release-tool tests:

```bash
./scripts/perception-sdk.sh check
python3 tools/perception/tests/test_release.py
```

Run affected runtime tests and builds when generated APIs are consumed by
runtime code:

```bash
./scripts/build-elements.sh debug true
meson test -C /work/development/build --print-errorlogs
```

Do not run `package` merely to validate implementation output. `check` is the
correct source-snapshot validation command.

## Prepare the Source Commit

Keep these files in the same normal source commit when they changed together:

- authored schemas and `tools/perception/sdk.json`
- the flowdata-sdk submodule pointer when intentionally updated
- `generated/perception/`
- `development/perception/meson.build`
- runtime integrations, tests, and documentation

Stage or commit only when the user requests it. Never include `artifacts/` in
the implementation commit.

## Report

State the authored inputs, generated outputs, SDK version, payload identity
changes, validation commands, and anything not run. Do not call the snapshot
commit-ready if `check` fails or generated files contain unexplained changes.
