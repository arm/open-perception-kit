---
name: evolve-perception-schema
description: Evaluate, add, or evolve Perception FlatBuffers schemas and integrate their runtime semantics. Use for changes under schemas/perception, new FrameResults payloads, compatibility reviews, schema or SDK version decisions, or complex features that require schema updates. This skill owns authored schema design and compatibility; use regenerate-perception-sdk for checked-in generated outputs and package-perception-sdk-release for distributable ZIP bundles.
---

# Evolve Perception Schema

Apply the repository's schema policy, make the smallest compatible authored
change, and define its runtime semantics before regenerating derived SDK files.

## Establish Context

1. Read the applicable `AGENTS.md` files.
2. Read `schemas/perception/README.md` completely.
3. Inspect `tools/perception/sdk.json`, affected `.fbs` files, and runtime
   producers and consumers.
4. Determine the comparison base. Prefer the target branch or its merge base;
   use `HEAD` only for evaluating uncommitted changes.
5. Run the bundled evaluator before editing:

   ```bash
   python3 .agents/skills/evolve-perception-schema/scripts/evaluate_schema_change.py \
     --base origin/develop
   ```

## Classify the Change

- **No schema change:** keep the SDK version unless public generated behavior
  changes.
- **Compatible addition:** append table fields or add a payload root; increment
  the SDK MINOR version.
- **Breaking change:** remove, rename, reorder, retype, or reinterpret published
  fields; increment SDK MAJOR and define migration behavior.
- **Unclear semantics:** stop and document the compatibility decision before
  editing.

Treat changes to `common.fbs` as schema-set-wide. FlowData payload IDs are
content-derived, so even a wire-compatible schema edit creates new typed payload
identities. Do not claim mixed-version typed compatibility without fixture tests.

## Implement Authored Inputs

1. Edit schemas only under `schemas/perception/metadata/`.
2. Preserve published field order, types, defaults, root types, and file
   identifiers unless intentionally making a breaking change.
3. Give every new transportable root a unique four-character file identifier,
   one `root_type`, and initial `schema_major` and `schema_minor` fields.
4. Update the SDK version and owned inputs only in
   `tools/perception/sdk.json`.
5. Update affected runtime producers, consumers, inspection tools, tests, and
   documentation.
6. Add old/new fixture tests when mixed-version behavior matters.

Do not edit `generated/perception/`, generated build integrations, or manifests
in this workflow.

## Hand Off Generation

After authored inputs and runtime semantics are settled, apply
`$regenerate-perception-sdk` to regenerate, inspect, and validate the checked-in
C++, Python, and TypeScript SDK snapshot. Commit authored and generated source changes
together through the normal repository workflow.

Do not build a release ZIP during implementation. After the complete snapshot
is committed and explicitly selected for release, apply
`$package-perception-sdk-release`.

## Validate Schema Decisions

Re-run the evaluator after editing and resolve every error:

```bash
python3 .agents/skills/evolve-perception-schema/scripts/evaluate_schema_change.py \
  --base origin/develop
```

Run tests for the runtime behavior that produces or consumes the payload. Let
`$regenerate-perception-sdk` own SDK drift checks and generated-source review.

## Report

Summarize the compatibility classification, required SDK version bump, changed
roots, transitively affected payloads, runtime semantics, migration behavior,
and fixture coverage. State whether generation has been handed off and whether
a release bundle is intentionally out of scope.
