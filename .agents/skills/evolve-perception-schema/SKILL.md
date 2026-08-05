---
name: evolve-perception-schema
description: Evaluate, add, or evolve Perception FlatBuffers schemas and their generated C++ and Python SDKs. Use for changes under schemas/perception, new FrameResults payloads, compatibility reviews, schema or SDK version decisions, SDK regeneration, release validation, or review-readiness assessment of schema changes.
---

# Evolve Perception Schema

Apply the repository's schema policy, make the smallest compatible change, and
validate generated artifacts through the canonical scripts.

## Establish Context

1. Read the applicable `AGENTS.md` files.
2. Read `schemas/perception/README.md` completely.
3. Inspect `tools/perception/sdk.json`, the affected `.fbs` files, and the
   runtime producers and consumers.
4. Determine the comparison base. Prefer the target branch or its merge base;
   use `HEAD` only for evaluating uncommitted changes.
5. Run the bundled evaluator before editing:

   ```bash
   python3 .agents/skills/evolve-perception-schema/scripts/evaluate_schema_change.py \
     --base origin/develop
   ```

## Classify the Change

Classify the requested change before implementation:

- **No schema change:** keep the SDK version unless public generated behavior
  changes.
- **Compatible addition:** append table fields or add a payload root; increment
  the SDK MINOR version.
- **Breaking change:** remove, rename, reorder, retype, or reinterpret published
  fields; increment SDK MAJOR and define migration behavior.
- **Unclear semantics:** stop and document the compatibility decision before
  editing.

Treat changes to `common.fbs` as schema-set-wide. The FlowData payload ID is
content-derived, so even a wire-compatible schema edit creates new typed payload
identities. Do not claim mixed-version typed compatibility without fixture tests.

## Implement

1. Edit authored schemas only under `schemas/perception/metadata/`.
2. Preserve published field order, types, defaults, root types, and file
   identifiers unless intentionally making a breaking change.
3. Give every new transportable root a unique four-character file identifier,
   one `root_type`, and initial `schema_major` and `schema_minor` fields.
4. Update only the SDK version and owned inputs in `tools/perception/sdk.json`.
5. Regenerate through the canonical command:

   ```bash
   ./scripts/perception-sdk.sh generate
   ```

6. Never hand-edit `generated/perception/`, generated build integrations, or
   manifests.
7. Update affected runtime producers, consumers, inspection tools, tests, and
   documentation in the same change.

## Validate

Re-run the evaluator after editing. Resolve every error and explicitly review
each warning:

```bash
python3 .agents/skills/evolve-perception-schema/scripts/evaluate_schema_change.py \
  --base origin/develop
```

Run schema and release checks:

```bash
./scripts/perception-sdk.sh check
python3 tools/perception/tests/test_release.py
PYTHONPATH="generated/perception/python/src:tools/plumber" \
  python3 -m unittest discover \
  -s tools/plumber/tests \
  -p 'test_frame_results_*.py'
```

For runtime-facing changes, also run:

```bash
./scripts/build-elements.sh debug true
meson test -C /work/development/build --print-errorlogs
```

For a release candidate, package and verify from a clean committed snapshot:

```bash
./scripts/perception-sdk.sh package \
  --output-dir artifacts \
  --expect-version <MAJOR.MINOR.PATCH>
./scripts/perception-sdk.sh verify \
  artifacts/perception-sdk-<MAJOR.MINOR.PATCH>.zip \
  --require-sidecars
```

## Report

Summarize:

- compatibility classification and required SDK version bump
- changed roots and transitively affected payloads
- runtime producers and consumers updated
- generated artifacts reviewed
- commands that passed
- commands not run and the concrete reason
- remaining migration or mixed-version risks

Do not call a change review-ready while required checks are failing, generated
output is stale, the version bump is insufficient, or compatibility behavior is
undefined.
