## 1. Shared validator

- [ ] 1.1 Add the pinned jsoncons dependency, one-pass duplicate-key-aware parsing, and one explicit
  Meson manifest that generates embedded and live schema bundles with offline `$ref` resolution.
- [ ] 1.2 Dispatch only from exact source basename and version, supporting non-empty Model and
  OpChain variants, using canonical source names for typed in-memory calls, and treating the
  caller's expected descriptor type as an assertion rather than another routing input.
- [ ] 1.3 Return `tl::expected<T, ValidationReport>` directly from the small typed facades, with
  stable issue fields and deterministic ordering; add no `Validated<T>`, rule registry, or public
  validation framework.
- [ ] 1.4 Keep schema loading, document validation, reporting, and repository traversal in cohesive
  private units; expose no repository validator or descriptor count from `Validator.h`, compile the
  repository helper only into the CLI and its focused test, enforce common C0/DEL/C1 descriptor-name
  safety, and allow repeated canonical names.
- [ ] 1.5 Add the thin text-only `pek-config-check` CLI with live-schema meta-validation, recursive
  supported-descriptor discovery, deterministic aggregation, side-effect-free reusable validators,
  logged and flushed CLI failures, and exit codes 0/1/2.

## 2. Model v1

- [x] 2.1 Complete the closed Model v1 schema, including all schema-expressible matrices and
  conditionals, finite-float projection bounds, local relative or absolute runtime `modelFile`
  paths, and the safe-relative `modelFile` requirement for optional closed `hfDownload` metadata.
- [ ] 2.2 Add the Model v1 semantic rules for control-free `modelFile`, checked tensor size,
  feedback destination, static source, and static dtype/shape compatibility without duplicating
  schema-owned rules.
- [ ] 2.3 Route Model production parsing through the common validator; retain the existing
  `fromFile` and backend interfaces, preserve authored in-memory paths, resolve relative file-backed
  paths from the descriptor directory without normalization, and omit build-only or unsupported
  fields from runtime projection and canonical JSON.
- [ ] 2.4 Make the downloader treat either `modelFile` or `hfDownload` as a Model candidate, validate
  every candidate with the live Model schema, prepare every schema-valid symlink-contained
  destination before network access, and report expected JSON, schema-reference, and filesystem
  failures once without a traceback.

## 3. OpChain v1

- [x] 3.1 Complete the OpChain root, shared Op, and built-in contracts: keep common attributes
  optional, require attributes only for exact `<library>/Inference` and `GenericPostprocess`, reserve
  exact `Inference` without backend allowlists, keep custom attributes open, and encode all 13
  registered parser contracts as local `$defs` in `GenericPostprocess`, sharing one definition for
  the four no-config parsers while bounding every parser value projected to `float`.
- [ ] 3.2 Add control-free Op-ID and Inference-path semantics plus loop grouping, controller-first
  ownership, built-in stage order and loop coverage, and low/high threshold rules using the shared
  exact-Inference-name predicate.
- [ ] 3.3 Route OpChain projection through the common validator, round-trip version and description,
  remove `group`, represent descriptor `loopId` with `std::optional`, omit it from JSON when absent,
  reject explicit zero, and convert omission to the scheduler sentinel only at runtime handoff.
- [ ] 3.4 Run the same typed OpChain semantics at the start of direct
  `OpChain::setupFromDescriptor`, before Op binding, and remove the later duplicate loop owner.
- [ ] 3.5 Preserve authored Inference references in memory; resolve relative file-backed references
  from the containing OpChain, leave absolute paths independently rooted, preserve filesystem
  component order through symlink-plus-parent traversal, and reject URI-like values.

## 4. Supported descriptors and consumers

- [ ] 4.1 Add descriptor versions, remove redundant type, `modelFamily`, and unsupported Model
  projection fields, apply the tensor/feedback cleanup, and preserve existing display names.
- [ ] 4.2 Remove Op `group` and unused built-in attributes, omit loop IDs from unlooped Ops, and put
  each non-empty-content built-in stage under one controller-first nonzero loop.
- [ ] 4.3 Rename the existing PaddleOCR detection and recognition descriptors to ordinary canonical
  Model variants, update only the affected detection reference, exercise the normal
  discovery/routing/schema/semantics/loader path, and add no model, routing exception, placeholder
  OpChain, or unsupported classification path.

## 5. Tooling and documentation

- [ ] 5.1 Replace the general Python `jsonschema` validation adapter with a subprocess call that
  prefers the local Meson CLI then `PATH`; copy schemas before the Docker Meson build, build the CLI
  from the checkout in the consolidated CI flow, and stage it in local tools plus final dev/CI
  images without prebuilt checkout artifacts.
- [ ] 5.2 Pin Python Draft 2020-12 validation only in build/developer layers that run or test the
  downloader; keep it out of runtime images.
- [ ] 5.3 Update contributor, architecture, downloader, and extension docs with minimal v1 examples,
  validation commands, rule ownership, the private repository boundary, scheduler first-loop
  behavior, and authored-versus-resolved path handling.
- [ ] 5.4 Make `AttributeMap` float/double getters accept JSON integer or double values and make
  `OrDefault` return a default only for an absent key, never a present type mismatch.
## 6. Permanent verification

- [x] 6.1 Keep one repository-native test that meta-validates every current v1 schema resource,
  rejects duplicate `$id` values, and resolves every local `$ref` offline.
- [ ] 6.2 Add positive and negative table-driven C++ coverage for every applicable
  common/Model/OpChain rule ID plus parse, routing, reports, empty variants, control-free runtime
  strings, finite projections, canonical round trips, repeated names, parser/Inference contracts,
  loop boundaries, and source-relative paths including symlink-plus-parent traversal, without fake
  plugins or legacy tombstone assertions.
- [ ] 6.3 Test repository discovery, live-schema loading, aggregation, exit codes, text logging, and
  the subprocess adapter only at the private CLI boundary; do not assert repository-count or other
  removed implementation details.
- [ ] 6.4 Add downloader regressions for the `hfDownload` path conditional, missing `modelFile`,
  dangling `$ref`, destination containment before network access, concise errors, and traceback-free
  exits.
- [ ] 6.5 Update supported-descriptor, runtime-loader, and pipeline tests for the v1 contract,
  including descriptor-only PaddleOCR recognition, then run the minimal supported pipeline smokes.
- [ ] 6.6 Review the complete task-owned diff with Ponytail and run formatting/static checks, strict
  OpenSpec validation, the complete descriptor gate, relevant debug builds, full Meson regression,
  downloader/adapter tests, documentation generation, and Jira/OpenSpec/implementation consistency.

## 7. Delivery

- [ ] 7.1 Commit and push each scoped stack layer under repository contribution rules.
- [ ] 7.2 Update the three stacked PRs against their direct bases, run required exact-head CI, record
  final verification, and address only failures introduced by the owning layer.
