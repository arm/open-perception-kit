## 1. Shared validator

- [ ] 1.1 Add the pinned jsoncons dependency, embedded/live schema bundle support, and one-pass
  duplicate-key-aware parser.
- [ ] 1.2 Add filename/version dispatch, stable issues/reports, deterministic ordering, and the thin
  `pek-config-check` CLI.
- [ ] 1.3 Allow backend-specific descriptors to share canonical model names while validating each
  descriptor independently.

## 2. Model v1

- [x] 2.1 Complete the Model v1 schema, keeping every schema-expressible matrix and conditional out
  of semantic C++.
- [ ] 2.2 Add the four Model v1 semantic rules: tensor size, feedback destination, static source,
  and static compatibility.
- [ ] 2.3 Route Model production parsing through the common validator and remove unsupported
  projection fields.

## 3. OpChain v1

- [x] 3.1 Complete the OpChain v1 schema, split shared Op structure and built-in Op contracts into
  local versioned resources by distinct attribute shape, keep common Op attributes optional, and
  retain closed built-in contracts with open custom Op attributes.
- [ ] 3.2 Add loop, built-in stage, and low/high threshold semantic rules before any Op binding.
- [ ] 3.3 Route OpChain production parsing through the common validator; add description/version
  round-trip, remove group, and omit the internal loop sentinel from JSON.
- [ ] 3.4 Run the same typed OpChain semantics at the start of direct
  `OpChain::setupFromDescriptor`, before any Op binding, and remove the later duplicate loop owner.
- [ ] 3.5 Require a controller-led built-in stage to be entirely unlooped or share one controller-
  first loop, and cover the valid boundary, partial-loop, and empty/non-empty-content invalid
  combinations with one semantic regression test.
- [x] 3.6 Add a repository-native static test that meta-validates every v1 schema resource, rejects
  duplicate `$id` values, and resolves every local `$ref` offline.

## 4. Supported descriptors and consumers

- [ ] 4.1 Add descriptor versions, remove the redundant type field, and apply feedback mode and
  Model field cleanup across the complete supported descriptor set while preserving valid display
  names.
- [ ] 4.2 Remove unused built-in Op attributes and add only the loops required by non-empty-content
  built-in stages.
- [ ] 4.3 Verify PaddleOCR detection uses the same discovery, routing, schema, semantics, and loader
  path as every other supported Model/OpChain pair.

## 5. Tooling and documentation

- [ ] 5.1 Remove Python `jsonschema`, replace validation with a subprocess adapter that prefers the
  local Meson CLI then `PATH`, copy schemas before the Docker Meson build, and copy the built CLI
  onto the final dev/CI image `PATH`.
- [ ] 5.2 Update contributor and architecture docs with minimal v1 examples, validation commands,
  semantic ownership, the scheduler's first-loop-element behavior, and the resolved/runtime
  boundary.
- [ ] 5.3 Make `AttributeMap` float/double getters accept JSON integer or double numbers and make
  `OrDefault` return defaults only for absent keys, never present type mismatches.

## 6. Permanent verification

- [ ] 6.1 Add table-driven C++ tests for parse/routing/report/common/Model/OpChain rule IDs and
  positive/negative behavior.
- [ ] 6.2 Update existing descriptor and runtime-loader tests for the supported v1 contract; add
  only a thin CLI/wrapper boundary test.
- [ ] 6.3 Review the spec and diff with Ponytail, then run strict OpenSpec validation, full descriptor
  validation, formatting/static checks, debug build, relevant Meson tests, and minimal pipeline
  smoke tests in the dev container.

## 7. Delivery

- [ ] 7.1 Commit and push each scoped stack layer under repository contribution rules.
- [ ] 7.2 Open or update the three stacked PRs against their direct bases, trigger relevant CI for
  each exact head, and address only failures caused by that layer.

## 8. Responsibility-boundary corrections

- [ ] 8.1 Keep the existing `ModelDescriptor::fromFile` and backend interfaces: validate the local
  source path, then resolve `modelFile` relative to its descriptor directory for runtime use.
- [ ] 8.2 Represent descriptor `loopId` omission with `std::optional`, reject a manually constructed
  explicit zero, and convert to the scheduler's zero sentinel only at the runtime handoff.
- [ ] 8.3 Remove the `/work/config` exception from the Inference schema and accept authored local
  relative or absolute model-descriptor references without rewriting valid checked-in values.
- [ ] 8.4 Review the corrected spec and diff with Ponytail, then run strict OpenSpec validation,
  descriptor validation, the relevant build/tests, and static checks in the dev container without
  committing.

## 9. Review follow-up corrections

- [ ] 9.1 Make the shared per-document pipeline select schema only from the exact source basename
  and version; keep typed expectations as assertions, use canonical default source filenames for
  in-memory calls, and reject empty OpChain variants.
- [ ] 9.2 Make descriptor names, Model `modelFile`, Op IDs, and exact `<library>/Inference` paths
  control-free in their common/Model-v1/OpChain-v1 semantic owners before C-string use; do not
  retain a partial schema pattern that misses embedded U+0000.
- [x] 9.3 Bound every schema field projected or cast to `float` to the finite `float` range while
  retaining narrower existing bounds.
- [ ] 9.4 Add minimal permanent regressions for production filename routing, empty variants,
  control-free names, finite numeric projection, and canonical round-trip validation.
- [ ] 9.5 Review the spec and implementation with Ponytail, then run strict OpenSpec validation,
  the complete descriptor/pipeline test matrix, builds, and static checks in the dev container
  without committing.

## 10. Fresh-develop rebase corrections

- [ ] 10.1 Align Model v1 with descriptor-relative `modelFile` and optional closed build-time
  `hfDownload`; reject legacy runtime locators and keep build metadata out of runtime projection.
- [ ] 10.2 Migrate the existing PaddleOCR detection and recognition files to normally discovered
  Model v1 descriptors; add no new model descriptor, routing exception, or unsupported placeholder
  OpChain.
- [ ] 10.3 Revalidate OpenSpec, review the complete rebased diff, and rerun the descriptor gate,
  builds, tests, static checks, and minimal pipeline smoke tests in the current dev container.

## 11. Descriptor-source-relative path contract

- [ ] 11.1 Accept only relative or absolute filesystem paths for Model `modelFile` and built-in
  Inference `modelDescriptor`; reject URI-like values.
- [ ] 11.2 Resolve relative file-backed references from the descriptor that contains them and
  preserve authored in-memory JSON plus valid checked-in relative or absolute values.
- [ ] 11.3 Run the focused loader/schema tests, complete descriptor gate, strict OpenSpec validation,
  build, full tests, and static checks.
- [ ] 11.4 Preserve filesystem component order in both file-backed loaders and cover symlink plus
  parent traversal without introducing lexical normalization or premature canonicalization.

## 12. Validator cohesion and permanent rule coverage

- [ ] 12.1 Split schema loading, document validation, reporting, repository validation, and the
  typed facade into coherent implementation units without adding a public framework.
- [ ] 12.2 Split tests along those boundaries and cover every artifact-free Model/OpChain semantic
  rule with supported positive and negative behavior.
- [ ] 12.3 Review the complete diff and run formatting, static analysis, full Meson regression,
  repository validation, CI adapter tests, and final Jira/OpenSpec/implementation consistency.

## 13. Integrated-develop reconciliation

- [ ] 13.1 Preserve the integrated removal of schema-invalid `modelFamily` while retaining existing
  descriptor display names under the v1 contract.
- [ ] 13.2 Support non-empty `model-<variant>.json` filenames so PaddleOCR's existing colocated
  descriptors remain ordinary Model descriptors without a special-case validator path.
- [ ] 13.3 Move the quality-gate build of `pek-config-check` to the consolidated CI container flow,
  keeping tool images free of prebuilt checkout artifacts.
- [ ] 13.4 Re-run strict OpenSpec validation, code review, containerized static analysis, complete
  regression tests, and exact-head ready-equivalent CI after the rebase.

## 14. Internal repository-validation boundary

- [ ] 14.1 Remove `validateRepository` and the repository-only, externally unused
  `descriptorCount` field from `Validator.h`; declare the helper in
  `RepositoryValidatorInternal.h` under `pek::config::detail`, included only by its implementation,
  `pek-config-check`, and its focused repository test. Correct the Python adapter documentation to
  distinguish CLI orchestration from the shared production per-document validator.
- [ ] 14.2 Remove `RepositoryValidator.cpp` from the static validator library linked by
  `pek-common`; compile it directly into the CLI and repository-test targets without adding another
  library, interface, or class hierarchy.
- [ ] 14.3 Keep repository discovery, live-schema, aggregation, and CLI behavior tests at the
  private CLI boundary; keep Model/OpChain parse, schema, and semantic tests at the shared public
  boundary, including acceptance of repeated canonical names without a catalog-only rule. Remove
  the repository-count implementation-detail assertion rather than replacing it with an absence
  test.
- [ ] 14.4 Review the focused diff, verify the production validator no longer exposes or links the
  repository helper, then run strict OpenSpec validation, config-validator and CLI tests, the
  complete descriptor gate, static checks, and the relevant debug build/regression suite.

## 15. Model-download schema convergence

- [x] 15.1 Make the Model v1 schema conditionally require a safe relative `modelFile` when
  `hfDownload` is present while retaining absolute and parent-traversing runtime paths otherwise.
- [ ] 15.2 Make the model downloader validate every consumed Model JSON with the same live schema and
  prepare all schema-valid, symlink-contained destinations before starting network access.
- [ ] 15.3 Pin the build/developer-only Draft 2020-12 dependency, update the user-facing contract, and
  add permanent C++ schema plus Python downloader regressions.
- [ ] 15.4 Run strict OpenSpec validation, focused tests, the descriptor gate, static checks, debug
  build and complete Meson regression; review the diff and stop before commit.

## 16. Validation error logging

- [ ] 16.1 Catch expected downloader schema, JSON, and filesystem failures at the executable
  boundary, log one concise error through Python logging, and exit nonzero without a traceback.
- [ ] 16.2 Keep reusable C++ validators side-effect-free; route human-readable `pek-config-check`
  failures through `pek::log::error()` with a flush while preserving clean JSON reports.
- [ ] 16.3 Add CLI regressions for logged text failures, traceback-free downloader errors, and clean
  failing JSON output.
- [ ] 16.4 Run strict OpenSpec validation and all focused/full checks, review every task-owned diff,
  and stop before commit.

## 17. Downloader schema-gate review fixes

- [ ] 17.1 Treat objects with either `modelFile` or `hfDownload` as Model candidates so incomplete
  download declarations cannot bypass schema validation.
- [ ] 17.2 Convert public schema-reference resolution failures into one concise logged schema error
  without a traceback or network request.
- [ ] 17.3 Add permanent downloader regressions for missing `modelFile` and dangling `$ref` cases.
- [ ] 17.4 Run strict OpenSpec validation, existing-container build/tests and static gates, and
  review all local changes before delivery.

## 18. Extensible parser and inference contracts

- [x] 18.1 Define subordinate parser schema ownership and the exact `<library>/Inference` extension
  contract in proposal, design, and specification; pass strict OpenSpec validation.
- [ ] 18.2 Cover all 13 registered parsers with one local resource per distinct attribute shape and
  generate the embedded/live schema inventory from one explicit Meson manifest.
- [ ] 18.3 Replace the four-backend inference allowlists with the shared exact-name predicate in
  schema validation, semantic validation, and file-backed path resolution.
- [ ] 18.4 Align extension documentation and add permanent parser/inference schema, semantics, and
  loader regressions without a fake runtime plugin or legacy tombstone tests.
- [ ] 18.5 Review the complete task-owned diff and pass formatting, strict OpenSpec, descriptor,
  build, focused static-analysis, documentation, and full Meson gates in the development container.
- [ ] 18.6 Verify the exact pushed implementation head through required quality, release/test, and
  clang-tidy CI, then record that verification in this task list to trigger final exact-head CI.
