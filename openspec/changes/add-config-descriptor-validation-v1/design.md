## Context

Before this change, `ModelDescriptor::fromJson` and `OpChainDescriptor::fromJson` parsed
independently with `nlohmann::json`, silently ignored unknown members, and could reach backend or
plugin setup before cross-field validation. The branch initially added schemas through a second
Python-only validator, which did not protect production or implement the requested common, Model
v1, and OpChain v1 semantic layers.

The Jira plan contained useful contract detail but also mixed artifact-free descriptor validation
with resolved backend/parser work and made assumptions that do not match the checkout. This design
keeps the coherent first increment and records the corrections explicitly.

## Goals / Non-Goals

**Goals:**

- One parse/schema/semantic implementation for production and tooling.
- One Model v1 schema source for schema-expressible rules in production, tooling, and build-time
  artifact materialization.
- Exact ownership: parser, schema, artifact-free C++ semantics, CLI-only repository orchestration,
  or existing resolved/runtime code.
- Stable, deterministic diagnostics and a complete supported-descriptor repository gate.
- Minimal downstream changes and permanent tests at the public validation and CLI boundaries.

**Non-Goals:**

- Backend capability matrices, artifact metadata introspection, parser tensor-output preflight, or
  hardware proof.
- Downloading or inspecting model artifacts during descriptor validation.
- A public rule plugin API, rule DSL, dependency graph, or changed-file optimization.
- A production-facing repository validator API or cross-descriptor catalog semantics.
- Automatic custom-loop termination proof or custom Op attribute introspection.

## Validated Corrections to the Jira Plan

| Plan statement | Repository evidence | Final decision |
|---|---|---|
| jsoncons rejects duplicate keys while building a DOM | jsoncons v1.7.0 keeps the first duplicate value | Add a private SAX forwarding filter in the same parse; reject before schema use |
| `modelFile` is relative-only | Supported descriptors use local paths; published single-file models may also declare build-time `hfDownload` metadata | Accept descriptor-relative or absolute filesystem paths and validate optional build metadata; do not restore runtime remote locators |
| OpChain `modelDescriptor` references use the process working directory | A relative reference otherwise changes meaning when an OpChain moves or is loaded outside the launcher | Resolve relative references from the OpChain descriptor directory and accept absolute filesystem paths; reject URI-like values |
| Tensor feedback requires new runtime mode behavior | Runtime implements only copy and the typed structure has no mode | Require canonical `"mode": "Copy"` in JSON; projection consumes/emits the constant |
| backend/preflight work belongs in the first schema freeze | It needs artifacts, optional SDKs, hardware, and new runtime metadata APIs | Keep it outside this artifact-free change |

## Rule Ownership

### Parse and dispatch

The parser owns JSON syntax, UTF-8 validity, and duplicate object keys. After one DOM is built, the
dispatcher selects exactly one offline schema from the descriptor filename (`model.json`,
`model-<variant>.json`, `opchain.json`, or `opchain-<variant>.json`, where the variant is non-empty)
and `version`. An
unsupported filename or missing or unsupported version fails in the `dispatch` phase before
type-specific projection. File-backed production and repository entry points pass their actual
path to this dispatcher. Typed in-memory entry points default their source to the corresponding
canonical filename; an explicitly supplied source is a routing path, not an arbitrary label. The
typed facade's expected C++ result type may reject a mismatched route but never selects the schema.

### JSON Schema

Schemas own every rule directly expressible in standard Draft 2020-12: types, required members,
enums, ranges, cardinality, closed objects, exact tensor layouts, field applicability,
same-document conditionals, built-in Op/parser attribute matrices, canonical loop syntax, and exact
duplicate feedback entries. Semantic C++ does not repeat these checks.

Every JSON number projected or cast to `float` is bounded to the finite `float` range in schema;
probability and letterbox fields already have narrower bounds. This keeps schema-valid typed values
finite and canonically serializable without a second C++ numeric-validation pass.

Descriptor names and values passed to filesystem or plugin APIs must be control-free. Draft 2020-12
can express that restriction with a pattern, but jsoncons delegates patterns to `std::regex`, whose
C-string boundary handling does not reliably match an embedded U+0000. The common semantic helper
therefore implements the complete C0/DEL/C1 restriction for each owning version-specific rule; the
schemas retain their structural string/path rules and do not duplicate a partial control pattern.

Because v1 caps tensor arrays and feedback indices at 16, destination/source existence,
destination kind, and destination-index uniqueness could technically be simulated with 16 repeated
`if`/`contains`/`prefixItems` branches. That is not a native cross-index constraint: it duplicates
the bound, produces indirect diagnostics, and makes the schemas substantially larger. Keep those
relationships in the single Model v1 semantic pass instead. Exact duplicate feedback objects remain
schema-owned through `uniqueItems`.

### Artifact-free C++ semantics

Direct version-specific functions and fixed rule-ID constants provide stable diagnostics without a
registry abstraction:

- common per-document: control-free descriptor names;
- Model v1: control-free `modelFile`; checked tensor element/byte counts; feedback destination,
  static source, and static dtype/shape compatibility;
- OpChain v1: control-free Op IDs and Inference descriptor paths; loop grouping; built-in
  controller ownership of the first loop position; built-in stage order/loop coverage; low/high
  threshold ordering.

Per-document rules are side-effect-free and run before runtime model-file use or Op-library binding.
They return structured failures and never log them, so callers retain control of user-visible error
handling. Equal descriptor names remain valid; there is no cross-descriptor semantic pass.

### CLI-internal repository orchestration

`pek-config-check` alone owns checkout schema loading and meta-validation, recursive discovery below
`config/models/` and `config/opchains/`, repeated calls to the shared per-document pipeline, and
deterministic aggregation. This is CI/tooling orchestration, not a production validation capability.

Keep the repository helper in the private `detail` namespace behind
`RepositoryValidatorInternal.h`, included only by its implementation, the CLI, and its focused unit
test. Remove its declaration and the repository-only, externally unused `descriptorCount` field
from `Validator.h`, and compile `RepositoryValidator.cpp` only into the CLI and repository-test
targets rather than the static validator library linked by `pek-common`. Do not introduce another
report type, library, interface, or class hierarchy for this separation. The Python adapter
continues to invoke the CLI as a subprocess and describes the shared boundary accurately:
production and CI reuse the per-document validator, not the repository helper.

The CLI owns the human-readable output boundary. Text validation failures and
invocation/internal failures use `pek::log::error()` and flush before exit, so configured PEK log
targets receive them. Help and successful reports remain direct terminal output. JSON reports,
including failing reports, remain clean stdout data and are not duplicated into the logger.
Build the existing `common/logging` target before the validator and link it only into the CLI; the
reusable validator library retains no logging dependency.

### Resolved/runtime

The image build consumes optional `hfDownload` metadata and attempts to place the named artifact at
the descriptor-relative `modelFile` path. When `hfDownload` is present, the Model v1 schema requires
`modelFile` to use the same safe-relative-path definition as other staged paths; absolute paths and
parent traversal remain valid only for non-downloaded runtime models. This metadata is
schema-validated but build-only, so the validated JSON projection and its canonical serialization
omit it. Runtime never interprets remote locators or credentials.

Before any network request, the Python downloader loads the live
`config/schemas/v1/model.schema.json`, validates every JSON object it would consume as a Model
descriptor, and prepares every destination. It does not reimplement lexical path rules. Its resolved
model-directory containment check remains only for filesystem state that JSON Schema cannot see,
such as a relative path crossing the boundary through a symlink. Externally managed absolute runtime
paths without `hfDownload` do not enter build staging. This narrow build-time schema consumer does
not replace the shared C++ production/CLI validation pipeline or add another repository validator.
Its executable boundary catches expected schema, JSON, and filesystem failures, sends one concise
message through Python's standard logger, and exits nonzero without a traceback.
An object with either a root `modelFile` or `hfDownload` member is a Model candidate, so an
incomplete download declaration cannot bypass validation by omitting `modelFile`. Schema reference
resolution failures use the public `referencing` exception surface and are reported as schema
errors without exposing a traceback or a full schema dump.

`ModelDescriptor::fromJson` publishes the validated runtime projection with the authored local
`modelFile` value. `ModelDescriptor::fromFile` keeps the existing interface, resolves a relative
value from the descriptor directory, and leaves an absolute path rooted independently; backends
continue consuming `modelFile` directly. The loader joins paths without lexical normalization or
canonicalization so symlinks followed by `..` retain normal filesystem semantics; target and
symlink-policy checks remain deferred to resolved/runtime validation.
Backend/artifact metadata, dynamic feedback compatibility, actual shape-match output existence,
explicit/automatic preprocess input selection, custom Op capability, parser registry/output
preflight, and first-frame checks are not guessed by this change.

The OpChain projection uses `std::optional<std::size_t>` for descriptor `loopId`; omission is
`std::nullopt`. The single descriptor-to-runtime handoff converts omission to the scheduler's
private zero sentinel. This keeps JSON state out of the runtime representation without expanding
the scheduler change.

Every exact `<library>/Inference` ID is the v1 inference extension point. Its only attribute is the
required local-filesystem `modelDescriptor`; runtime implementations must expose the existing
`OpInterfaceInference` contract. `fromJson` keeps the reference's
authored value for canonical validation and serialization. `OpChainDescriptor::fromFile` resolves
relative values from the OpChain descriptor directory before Op configuration and leaves absolute
values rooted independently. It does not lexically normalize or canonicalize the result, preserving
filesystem component order and symlink-aware parent traversal. URI-like values are rejected; no
process-working-directory meaning remains for relative values, while absolute values retain their
authored root. Plugin/factory loading, interface conformance, artifacts, and backend compatibility
remain runtime checks. An Op with a different contract must not use the reserved `Inference`
operation name; names such as `InferenceLike` remain ordinary custom Ops.

## Architecture

Keep the public, versioned configuration contract under `config/schemas/`; Op implementation
directories do not own or install it. Split OpChain v1 at actual contract boundaries:

```text
config/schemas/v1/
├── model.schema.json
├── opchain.schema.json
└── opchain/
    ├── common.schema.json
    ├── op.schema.json
    └── ops/
        ├── inference-controller.schema.json
        ├── generic-image-preprocess.schema.json
        ├── inference.schema.json
        ├── generic-postprocess.schema.json
        └── generic-postprocess/
            ├── camera-contact.schema.json
            ├── ...
            └── yolo-x.schema.json
```

`opchain.schema.json` owns only the descriptor root. `op.schema.json` owns the common Op shape and
composes the built-in Op resources. The Inference resource selects the reserved exact
`<library>/Inference` shape and owns its shared closed attribute contract, independent of the
currently available backend libraries. `GenericPostprocess` remains the owning Op and dispatches
its closed `attributes` object through one local subordinate resource per registered parser. These
resources are not standalone Ops. Adding a parser requires its resource, one parent dispatcher
reference, and one schema-bundle manifest entry; no registry framework or plugin introspection is
introduced.

The model downloader reads `model.schema.json` from this same tree rather than copying path rules
into Python. The model-build and developer images pin the standard Python Draft 2020-12 validator;
runtime images do not contain the downloader or this dependency.

Add `development/config-validator/` before `development/common/` in the Meson graph:

```text
config-validator/
├── DocumentValidator.cpp
├── meson.build
├── ModelV1.cpp
├── OpChainV1.cpp
├── RepositoryValidator.cpp
├── RepositoryValidatorInternal.h
├── SchemaBundle.cpp
├── ValidationReport.cpp
├── Validator.cpp
├── EmbeddedSchemas.h.in
├── Validator.h
├── ValidatorInternal.h
└── pek-config-check.cpp
```

Tests follow the same document, Model, OpChain, CLI-repository, and report boundaries.
`Validator.cpp` contains only the small typed public facade; its two entry points pass the expected
descriptor type to one shared function, and the existing jsoncons visitor remains the one justified
polymorphic boundary.

The runtime validator library depends on jsoncons, GLib, and the existing header-only descriptor
projections, but not on `pek-common` or GStreamer. `pek-common` links that library. Production
descriptor entry points validate first and adapt a failed report to `pek::Result`; the CLI links the
same per-document validator and privately adds repository traversal before serializing its report.

The validator surface consumed by production contains only descriptor validation, typed OpChain
semantic validation for direct setup, `ValidationIssue`, `ValidationReport`, and immutable typed
`Validated<ModelDescriptor>` / `Validated<OpChainDescriptor>` results. Repository traversal, both
DOM representations, rule functions, and schema-bundle details remain private. After jsoncons has
checked syntax, duplicate keys, routing, and schema conformance, the existing nlohmann parser and
typed projection create the private typed candidate; no custom cross-DOM converter is maintained.
No typed value is published before its applicable semantics succeed.

The typed JSON entry points use `model.json` and `opchain.json` as their default source paths.
Callers that supply a source provide a path whose basename participates in routing; file-backed
production callers therefore cannot bypass filename dispatch by choosing a typed facade.

One explicit Meson manifest lists every schema resource ID and repository-relative path and
generates the embedded `{id, path, text}` array. Production resolves `$ref` only from that local
bundle. The CLI uses the same array to read and meta-validate the checkout's current schema files,
then passes them to the same offline resolver and engine so a running dev container does not
validate edits against stale embedded content.

## Diagnostics

Each issue contains stable `rule`, `phase`, `file`, `instanceLocation`, optional
`relatedInstanceLocation`, and `message`. A parser may include a source line/column in the message.
Reports sort by file, phase (`parse`, `dispatch`, `schema`, `descriptor`), instance location, and
rule. CLI JSON has `diagnosticFormatVersion: 1`. Help, successful reports, and every JSON report use
direct stdout without logger prefixes. Human-readable validation failures and invocation/internal
diagnostics use `pek::log::error()` and are flushed before exit; their console form may carry the
configured logger prefix. Exit codes are 0 valid, 1 validation failure, and 2 invocation/internal
failure.

## Supported Configuration Migration

- Add `version` and remove the redundant type marker from every supported descriptor.
- Apply the Model tensor matrix, remove ignored `zeroPoint`, `scale`, `outputDtype`, and Uint8
  mean/std values, and add canonical feedback mode.
- Retain develop's removal of `modelFamily`; identity is the canonical descriptor `name`.
- Validate optional build-time `hfDownload` objects. Rename the existing PaddleOCR detection and
  recognition descriptors to canonical `model-<variant>.json` filenames without adding models.
- Remove Op `group`, omit `loopId` for non-loop Ops, remove unused built-in attributes, and put every
  non-empty-content built-in stage under one nonzero loop.
- Preserve valid authored relative and absolute `modelDescriptor` references. Update only the
  PaddleOCR detection reference because its descriptor filename must become canonical
  `model-detection.json`.
- Keep PaddleOCR detection on the same `model-<variant>.json`/`opchain.json` routing, schema,
  semantic, and production-loading paths as every other supported model.
- Do not add a descriptor or placeholder OpChain for PaddleOCR classification; recognition remains
  descriptor-only until its postprocessing is supported.

## CI and Local Workflow

Pin jsoncons v1.7.0 at commit `cb54cdc3134a62634466bf7bcd24f1a906f4ef25` and source SHA-256
`5b24c82df11ac8fe4a0eaa55c9f991ba076ea6c6ceb6ecb7d841e36a7eacf3cb`. The CI quality services
build `pek-config-check` from the checked-out source in an isolated temporary Meson directory
before `expkits-ci --config-schema-check` invokes it. Normal local builds stage the same executable
under `tools/`. Pin Python `jsonschema` 4.26.0 only in the build/developer layers that execute or test
the model downloader. Full scanning is intentionally simpler than reverse-dependency logic at this
scale.

## Risks / Trade-offs

- [Schema and typed projections drift] → production and the CLI use the same schema resources and
  per-document engine before projection; representable numeric bounds preserve finite typed values,
  and round-trip tests cover both descriptor types.
- [Embedded schema can be stale during local editing] → the CLI supplies the live
  meta-validated schema bundle to the same engine.
- [CLI-only traversal leaks into production] → remove it from `Validator.h` and the runtime
  static-library source list; compile it only where the CLI and repository tests require it.
- [Semantic rules grow into a framework] → use direct fixed functions; introduce a registry,
  resolved context, or extension API only with a concrete need.
- [Build metadata leaks into runtime ownership] → validate `hfDownload` structurally, but omit it
  from the runtime projection; runtime consumes only the local resolved `modelFile`.
- [Schema engines disagree] → both consumers execute the checked-in Draft 2020-12 resource; permanent
  tests exercise the download-path conditional through the C++ validator and the Python downloader.
- [This does not prove artifacts/backends/parsers] → document the boundary and retain those checks
  in their current resolved/runtime owners.

## Migration Plan

1. Land the validator library/CLI and tests while retaining current supported behavior.
2. Tighten schemas and projections, then minimally migrate all supported descriptors to v1.
3. Route production entry points through validation and move the late loop checks before binding.
4. Replace Python validation with the CLI adapter and build the CLI into the CI dev image.
5. Internalize repository traversal without changing CLI behavior or adding a new abstraction.
6. Update docs, run the complete descriptor gate, build, relevant runtime tests, and PR CI.

Rollback removes one library/CLI and restores the descriptor files; there is no persisted runtime
state or external data migration.
