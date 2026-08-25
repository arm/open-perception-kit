## Why

Model and OpChain descriptors are a public configuration surface. Before this change, production
loaders and CI did not share a versioned structural and semantic contract. EXPKITS-762 adds the
first supported contract to reject invalid configuration before plugin or model-artifact setup.

## What Changes

- Add a Draft 2020-12 schema for Model v1 and a versioned local schema bundle for OpChain v1.
- Keep `GenericPostprocess` validation closed while dispatching registered-parser attribute shapes
  through local `$defs`; the four no-config parsers share one definition.
- Reserve the exact `<library>/Inference` Op ID shape for the shared v1 inference contract so a new
  runtime backend does not require a central schema or semantic-validator allowlist.
- Require `version` in every supported descriptor and route by the descriptor filename and
  version, with exact non-empty `model-<variant>.json` and `opchain-<variant>.json` forms.
- Add one C++ validation pipeline used by production loaders and the `pek-config-check` CLI.
- Add a repository-native schema-resource check for Draft 2020-12 validity, unique `$id` values,
  and offline local-reference resolution.
- Reject control characters in descriptor values that cross GLib, filesystem, or plugin C-string
  boundaries, and reject JSON numbers that cannot be represented by their typed `float` consumers,
  so every published validated value remains safe and canonically serializable.
- Keep reliably schema-expressible rules in the schemas; add only cross-value, cross-item, sequence,
  overflow, and C-string safety rules as per-document C++ semantics.
- **BREAKING**: close supported Model objects and built-in Op attributes, remove unused descriptor
  fields and attributes, remove Op `group`, and add only required loop declarations. Preserve
  existing display names and valid local `modelDescriptor` values; references may be relative to
  their OpChain source or absolute.
- Validate optional build-time `hfDownload` metadata, require its `modelFile` to be a safe relative
  destination, and make the model downloader consume the same live Model v1 schema before network
  access while keeping non-downloaded runtime `modelFile` values relative or absolute.
- Represent an omitted descriptor `loopId` without a numeric sentinel.
- Make `expkits-ci --config-schema-check` a thin launcher of `pek-config-check`; keep checkout
  discovery, live-schema loading, and report aggregation internal to that CLI.
- Correct the existing PaddleOCR recognition descriptor to `[1,3,48,640]`, preserving its concrete
  width while matching the ONNX input's fixed height and dynamic-width contract.
- Route validation failures through the standard Python or PEK logger at each CLI boundary while
  keeping reusable validators side-effect-free and text reports deterministic.
- Update contributor documentation with minimal v1 descriptors and the validation boundary.

## Capabilities

### New Capabilities

- `config-descriptor-validation-v1`: Versioned Model and OpChain structure, artifact-free semantics,
  production loading, internal repository CLI validation, and diagnostics.

### Modified Capabilities

None.

## Impact

- Adds a small C++ validator library and CLI under `development/config-validator/`.
- Adds one pinned header-only JSON Schema dependency.
- Updates Model and OpChain projections, production load entry points, and the late OpChain loop
  check.
- Migrates supported descriptors under `config/models/` and `config/opchains/` only as required by
  v1, preserving display names, valid existing absolute references, model artifacts, and parser
  thresholds.
- Replaces the repository-wide Python schema implementation with a subprocess adapter to the C++
  CLI; the build-only downloader remains a narrow consumer of the same Model v1 schema resource.
- Adds a pinned Python Draft 2020-12 implementation only to the model-build and developer images.
- Keeps repository traversal out of the production validator API and link target; the CLI and its
  focused tests alone own that helper.
- Keeps plugin loading, `OpInterfaceInference` conformance, model artifacts, and backend capability
  checks at runtime; schema validation owns only the common descriptor contract.
- Does not add backend/artifact introspection, parser-output preflight, resolved target existence,
  changed-file dependency graphs, or hardware tests.
