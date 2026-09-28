---
title: Configuration Compatibility
sidebar_label: Configuration Compatibility
sidebar_position: 3
description: Configuration version ownership, compatibility guarantees, dependencies, and migration.
---
<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
SPDX-License-Identifier: Apache-2.0

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->


# Configuration compatibility

Every pipeline preset, OpChain, and model descriptor must declare a string
version in `MAJOR.MINOR.PATCH` form, initially
`"version": "1.0.0"`. This configuration version is independent of the OPK
product version, model artifact format, dependency package versions, and
FrameResults payload format.

## Where to update the supported version

**Each descriptor JSON Schema owns its `x-opk-supported-version` value.**
Python and C++ readers load their supported version from that schema; they do
not maintain independent version tables. `tools/config_versions.py` contains only
the shared Python comparison logic.

When releasing a new configuration-format version, update the affected schema
metadata together with the format implementation, schemas, tests, and migration
notes. Do not add supported-version literals to individual readers. Changing
this metadata selects compatibility expectations; it does not implement new fields
or convert existing files. For a new major, update the corresponding schema
resources/dispatch and semantic validation as part of implementing that format.

Native validation reads this metadata from its embedded schemas. Python model
provisioning and SDK/setup/release readers load it from the corresponding schema
file. Rebuild native binaries after changing a runtime schema; Docker copies the
SDK schema with its installed helpers. SDK generation records schema and tool
hashes, so run `./scripts/perception-sdk.sh generate` and `check` after changing
SDK tooling or schemas.

JSON Schemas validate version **syntax**; readers apply major/minor compatibility
against the shared table before schema validation. A standalone schema check is
not a replacement for the OPK configuration checker.

An individual file's patch-only edit is not a new format release: increment that
file's patch, not the table or unrelated files. The table's patch identifies the
released format revision but is ignored when checking compatibility. OPK product
releases remain independently versioned in `development/meson.build`.

## Version ownership

| Contract or dependency | Authoritative source | Owner and compatibility boundary |
| --- | --- | --- |
| Pipeline JSON | `config/schemas/v1/pipeline.schema.json` | OPK launcher maintainers own the preset format and execution semantics. |
| OpChain JSON | `config/schemas/v1/opchain.schema.json` and its referenced Op schemas | OPK Op maintainers own ordering, loops, and built-in attributes. Custom Op authors own their attributes. |
| Model JSON | `config/schemas/v1/model.schema.json` | OPK model integration maintainers own tensor and preprocessing declarations. Model authors own the artifact and its export requirements. |
| SDK descriptor JSON | `tools/perception/sdk.json` and `tools/perception/sdk_config.py` | SDK tooling maintainers own its dependency locks, generator inputs, and release fields. It has no configuration-format version. |
| OPK runtime, native plugins, and SDK package version | `development/meson.build` | OPK maintainers version and distribute these together. Use components from the same release or source checkout. |
| Inference frameworks | `Dockerfile` dependency versions and `development/ops-*/meson.build` build integration | Backend maintainers select the packaged framework. Exported models must be supported by that framework. |
| Open Perception Kit and FlatBuffers | `schemas/perception/`, `tools/perception/sdk.json`, and the generated SDK manifest | Schema maintainers own payload semantics; the SDK descriptor owns generator inputs and exact FlatBuffers artifacts. |
| Embedded Python dependencies | `development/ops-python/runtime.json` and `tools/perception/sdk.json` | Python Op maintainers own NumPy; the SDK descriptor owns FlatBuffers. Use the supplied container or matching binary release runtime. |

JSON Schema resources are embedded into the native validator at build time.
Editing a schema file does not teach an already built launcher or runtime a new
format. Rebuild after changing the authored schemas. The repository checker
reads the live schema bundle so CI can validate those changes too.

## Compatibility rules

Readers compare the file version with the contract version they implement:

| Component | Mismatch behavior | When authors change it |
| --- | --- | --- |
| Major | Fail with an error before execution or dependency installation. Both older and newer incompatible majors are rejected. | Incompatible changes to fields, types, meanings, or defaults. |
| Minor | Emit a warning and continue validating and running the configuration. This applies to any minor mismatch, not just a newer file. | Backward-compatible contract additions. |
| Patch | Ignore for runtime compatibility; a patch-only difference produces no warning. | Increment for every edit to an existing configuration file within the same major/minor. |

For a reader supporting `1.0.0`, `1.0.37` is accepted silently, `1.4.2` emits a
warning and continues, and `0.9.0` or `2.0.0` fails. A minor warning does not
disable other checks: malformed fields, missing required fields, unavailable
dependencies, and unsafe commands still fail normally.

Use three nonnegative decimal components without leading zeroes, a `v` prefix,
prerelease suffix, or build metadata. Missing versions, legacy integers, and
malformed strings fail. OPK does not guess a version or rewrite a file while
loading it. Model and OpChain serialization preserves the full input version.

Each file has its own patch counter. Start newly versioned files at `1.0.0`.
When editing a command, model path, tensor declaration, dependency lock, or even
an inert annotation, increment that file's patch in the same change; do not
bump unrelated files. A new major or minor contract starts at patch zero and
must include reader/schema changes, tests, and migration documentation. Patch
numbers track file revisions, not OPK releases or model accuracy. Readers do
not compare patch history. For manually authored Pipeline, Model, and OpChain
files, the author owns the bump when changing a versioned descriptor.

The schema directory and resource IDs retain `v1` to identify the supported
major. Rebuild native consumers after changing their supported contract or
schemas. Contract families evolve independently.

Model and OpChain schemas
reject unknown top-level fields. Pipeline v1 preserves the existing convention
that extra top-level fields are inert annotations: `alternative-source-*`,
`alternative-sink-*`, and review annotations do not run. Only `pipeline`,
`description`, and optional boolean `loop` configure execution. Do not put
required features or dependency constraints in annotation fields.
`loop` controls the launcher only; applications using the C++ `Pipeline` API
handle end-of-stream themselves.

## Support-script inputs

Support scripts consume the checked-in SDK dependency descriptor for Docker
and LXC setup, FlatBuffers installation, and Python-runtime setup. SDK
generation, release validation, and bundle verification load the complete
descriptor through `load_sdk_config()`, which validates its dependency and
release fields. The SDK descriptor is not a Pipeline, Model, or OpChain
configuration contract, so it does not use their configuration-version
compatibility rules.
Model provisioning through `scripts/download-models.py` validates the Model
schema before downloads, rejects incompatible majors and malformed versions,
and warns for minor mismatches without treating them as errors.

CI/agent configuration and profile JSON files, the clang-tidy baseline, and
`development/ops-python/runtime.json` intentionally have no top-level
configuration version. The NumPy package version inside `runtime.json` remains
an exact dependency lock, not a configuration-format version. Do not add
versioning to these excluded files.

Externally owned formats (Compose, GitHub Actions, VS Code, Dev Containers,
package manifests, and documentation-site inputs) retain their owning tools'
formats. Generated receipts, media, labels, payloads, and local UI state are
not additional authored configuration contracts.

## Framework and SDK compatibility

Configuration validation does not establish model binary compatibility. For
example, an ONNX model's IR/opset and operators must be supported by the
packaged ONNX Runtime, and an ExecuTorch export must match its supported
runtime. Record the export tool, framework version, model format/opset, and
tested OPK release in the model's README or `index.md`. Retest the model when
upgrading those dependencies. The OpChain `runtime` field is a display label;
the inference Op's `id` selects the backend.

Native plugins and embedded Python postprocessors use the runtime and SDK
provided by the same OPK release. A configuration version does not negotiate
native ABI compatibility or install Python packages. For reproducible
deployments, keep the configuration, model artifacts, scripts, and matching
OPK container image or binary release together.

External consumers should start with the Open Perception Kit SDK shipped with their
producer. FlatBuffers wire compatibility, generated payload identity, and
SDK API compatibility are separate checks: an additive schema change can
produce a new payload ID that an older SDK cannot decode through its typed API.
See the FrameResults schema compatibility policy beside the schema definitions.
Changing a configuration `version` does not change the SDK or payload identity.

## Validation before execution

`opk-menu` validates the selected pipeline before expanding environment
variables or starting `gst-launch-1.0`, including for dry runs. The C++
`Pipeline::fromJsonFile` and `loadFromJsonFile` APIs use the same validator
before creating GStreamer elements. The contract requires a
nonblank pipeline string or an array of strings containing a nonblank command;
empty array entries are allowed. Embedded NUL bytes in commands are rejected.
The launcher joins array entries with spaces, then tokenizes the command and
expands the supported environment-variable forms.

OpChains are validated when loaded, before binding their Ops. Model descriptors
are validated before their inference backend loads the artifact. These checks
also apply when GStreamer starts an OpChain directly, and when `opkinfer` is
initially inactive. Incompatible major or malformed versions produce a diagnostic
with the file, `/version`, supported version, and migration guidance. Minor
mismatches emit a warning identifying the file and versions. Configuration validation
does not execute models or prove that plugins, artifacts, devices, or outputs
are available; backend setup and a real pipeline run provide that evidence.

Check all JSON files recursively under `config/pipelines`, `config/opchains`,
and `config/models` from the development container:

```bash
opk-ci --config-schema-check
```

The same check is available as `./tools/opk-config-check --root .` after a build.
Validate an external pipeline without executing it:

```bash
./tools/opk-menu -p /path/to/pipeline.json
```

This dry run validates the preset only; referenced OpChains and models are
validated when the pipeline starts.

## Migrating existing configurations

OpChain `1.0.2` adds the optional YoloParser `outputFormat` attribute. Existing
OpChains keep the `centerClassScores` behavior when it is omitted. Models that
produce `[1,N,6]` rows containing corner coordinates, confidence, and class ID
must set `"outputFormat": "cornerScoreClass"`.

Earlier pipeline presets had no version, or used the integer `1`. Preserve
their command and options and set `"version": "1.0.0"`. If an external preset used only the C++ loader and
omitted `description`, add a string description as well:

```json
{
  "version": "1.0.0",
  "description": "Minimal video test",
  "pipeline": ["videotestsrc num-buffers=1 !", "fakesink"],
  "loop": false
}
```

Existing model and OpChain files using `"version": 1` must also migrate to
`"version": "1.0.0"`. The SDK descriptor is not part of this configuration
version contract and must keep its own unversioned format. For an unversioned
external Model, OpChain, or pipeline descriptor, compare it with the corresponding
schema and checked-in example before adding `version`; the number alone does
not convert old field semantics. There is no automatic major-version converter.
For an incompatible major, use matching readers or perform the documented
field and behavior migration before changing the version number.

After migrating, run the repository checker, dry-run the pipeline, and then run
it with the intended model artifacts and inputs. Keep the original files and
matching runtime available until the migrated pipeline has been verified.
