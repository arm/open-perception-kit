## ADDED Requirements

### Requirement: One versioned validation pipeline

The system SHALL validate every supported Model and OpChain descriptor through one C++
per-document pipeline: duplicate-aware JSON parse, filename/version dispatch, offline Draft 2020-12
schema, private typed projection, and the applicable Model v1 or OpChain v1 semantics. Production loaders and
`pek-config-check` SHALL call the same implementation. Validation SHALL finish before runtime
model-file use or Op-library binding. `pek-config-check` SHALL privately orchestrate live-schema
validation, descriptor discovery, repeated per-document validation, and report aggregation. This
repository orchestration SHALL NOT be part of the production validator API and SHALL NOT add a
cross-descriptor semantic layer.

Reusable C++ validation functions SHALL return structured reports or errors without logging. The
outer executable or runtime owner SHALL log a failed operation once through its standard logging
API; validation helpers SHALL NOT duplicate that side effect.

Every supported descriptor SHALL contain `version` equal to 1. `model.json` and
`model-<variant>.json` filenames SHALL select Model validation; `opchain.json` and
`opchain-<variant>.json` filenames SHALL select OpChain validation, where `<variant>` contains at
least one character. The basename SHALL end exactly in `.json`; `model-.json` and
`opchain-.json` are unsupported. Schema selection SHALL use only this filename convention
and `version`, without a model-specific exception. File-backed production entry points SHALL route
their actual path. Typed in-memory entry points SHALL default their source path to `model.json` or
`opchain.json`; an explicitly supplied source SHALL be treated as a routing path. An expected typed
return value MAY reject a mismatched route but SHALL NOT select the schema. The supported schemas
SHALL be:

- `config/schemas/v1/model.schema.json`,
  `$id: urn:arm:pek:schema:model-descriptor:v1`;
- `config/schemas/v1/opchain.schema.json`,
  `$id: urn:arm:pek:schema:opchain-descriptor:v1`.

The OpChain root schema SHALL reference local versioned resources under
`config/schemas/v1/opchain/`. Those resources SHALL encapsulate the shared Op structure and each
supported built-in Op's ID selection and attribute contract. The `GenericPostprocess` resource SHALL
dispatch `attributes` through one subordinate local resource per registered parser; each parser
resource SHALL own that parser's closed attribute object. The exact `<library>/Inference` ID shape
SHALL select one shared Inference resource without enumerating backend libraries.

Every schema resource SHALL use the same duplicate-aware parser and SHALL compile against the
built-in Draft 2020-12 meta-schema. Only IDs declared by the local schema bundle and the built-in
meta-schema may resolve; network resolution SHALL never occur.

#### Scenario: Model v1 routing
- **WHEN** `model.json` or `model-<variant>.json` declares `version: 1`
- **THEN** the common and Model v1 validation layers run

#### Scenario: OpChain v1 routing
- **WHEN** `opchain.json` or `opchain-<variant>.json` declares `version: 1`
- **THEN** the common and OpChain v1 validation layers run

#### Scenario: Unsupported filename or version
- **WHEN** the descriptor filename is unsupported or `version` is missing or unsupported
- **THEN** validation fails before typed projection or setup

#### Scenario: Production filename determines type
- **WHEN** a Model payload is loaded through `ModelDescriptor::fromFile` from a filename outside
  `model.json` and `model-<variant>.json`
- **THEN** dispatch fails before runtime path resolution even when the payload satisfies Model v1

#### Scenario: Empty OpChain variant
- **WHEN** repository or production validation receives `opchain-.json`
- **THEN** dispatch reports an unsupported filename

#### Scenario: Empty Model variant
- **WHEN** repository or production validation receives `model-.json`
- **THEN** dispatch reports an unsupported filename

#### Scenario: Duplicate object key
- **WHEN** an object repeats a key at any nesting depth
- **THEN** parse validation reports the duplicate and the document does not reach schema validation

#### Scenario: Repository orchestration stays internal
- **WHEN** production code consumes the validator dependency
- **THEN** it can invoke descriptor validation without exposing or linking the CLI's repository
  traversal helper

### Requirement: Exact rule ownership

Standard Draft 2020-12 schemas SHALL own JSON types, required members, enums, ranges, cardinality,
closed objects, exact layouts, field applicability, same-document conditionals, and exact
`uniqueItems`. C++ semantic rules SHALL NOT repeat schema checks. They SHALL own only overflow,
cross-value/cross-index comparison and sequence/grouping for which the standard schema has no
direct relational keyword, plus complete control-character checks for
values that cross C-string boundaries because the selected implementation's regex engine cannot
enforce them for embedded U+0000. Bounded
cross-index rules SHALL NOT be duplicated into hand-unrolled schema branches.

#### Scenario: Schema-expressible failure
- **WHEN** a descriptor violates a type, range, field matrix, layout, or conditional
- **THEN** a schema issue is reported and no equivalent descriptor-phase issue is emitted

#### Scenario: Cross-value failure
- **WHEN** a structurally valid descriptor violates an applicable cross-value invariant
- **THEN** exactly the applicable version-specific semantic rule reports it

### Requirement: Common descriptor semantics

Descriptor names SHALL contain no C0, DEL, or C1 control character (U+0000–U+001F or
U+007F–U+009F). Equal names SHALL remain valid in every validation mode because backend-specific
Model descriptors intentionally share canonical model identities; no repository-only uniqueness
rule SHALL be added.

#### Scenario: Backend variants share a name
- **WHEN** two valid Model descriptors use the same canonical model name
- **THEN** `pek-config-check` accepts both descriptors

#### Scenario: Control character in a descriptor name
- **WHEN** a Model or OpChain name contains a C0, DEL, or C1 control character
- **THEN** the common per-document semantic check reports `common.v1.name-control`

### Requirement: Model descriptor v1 schema

Model v1 SHALL require `version: 1`, canonical trim-nonempty `name`, `modelFile`, `dynamicOutput`,
and 1–16 `inputTensors`. A trim-nonempty schema
string SHALL begin and end with a character other than U+0009, U+000A, U+000D, and U+0020, and
therefore contain at least one such character. Model v1 SHALL allow only `legal`, `contentType`,
`outputTensors`, `tensorFeedbacks`, and `hfDownload` as optional root members. `modelFile` SHALL be
a local filesystem path that is either relative to the Model descriptor or absolute. URI schemes,
including legacy `hf:` locators, SHALL fail schema validation. Parent traversal is a valid relative
filesystem path component only when `hfDownload` is absent.

When present, build-only `hfDownload` SHALL be a closed object containing exactly required
trim-nonempty `repo_id`, full 40-lowercase-hex `revision`, and safe relative `filename` strings.
The same-document conditional SHALL require `modelFile` to satisfy `safeRelativePath`, rejecting
absolute paths and parent traversal, whenever `hfDownload` is present. Descriptor validation SHALL
not access the network or require the artifact to exist. Descriptors without `hfDownload` SHALL not
be subjected to this staging-only lexical rule.

After schema projection, `model.v1.model-file-control` SHALL reject C0, DEL, and C1 control
characters in `modelFile` before filesystem or model-library use.

#### Scenario: Control character in a model artifact locator
- **WHEN** `modelFile` contains a C0, DEL, or C1 control character
- **THEN** `model.v1.model-file-control` rejects it before filesystem or model-library use

The schema SHALL express the supported input kind/dtype/field matrix:

- RGB CHW is `[1,3,H,W]` with floating dtype;
- RGB HWC is `[1,H,W,3]` with Uint8 or floating dtype;
- grayscale is `[1,1,H,W]` or `[1,H,W,1]` with Uint8 or Float32;
- raw tensor data has a positive 1–8 dimension shape and any canonical dtype;
- Value/Vector2/Vector3/Vector4 have exactly 1/2/3/4 values and effective Float32;
- mean/std are floating-image-only, aspect/letterbox fields are image-only, and letterbox fields
  require explicit `keepAspectRatio: true`;
- mean/std use only a closed `r`, `g`, `b`, optional `a` object or a three/four-number array; std
  RGB components are positive and letterbox components are in `[0,1]`;
- `matchShapeOutputIndex` is raw-input-only and requires dynamic output;
- static outputs are 1–16 raw tensors with explicit dtype and positive shape;
- dynamic output has no declared output tensors;
- feedback contains `mode: "Copy"` and 0–15 source/destination indices, with at most 16 entries.

Every JSON number stored in or cast to a C++ `float` SHALL lie in the inclusive finite range
`[-3.4028234663852886e38, 3.4028234663852886e38]`. This applies to mean/std components and
Value/Vector input values; their narrower positivity or cardinality rules remain in force.

Canonical descriptor dtypes SHALL be `Uint8`, `Int8`, `Float16`, `Float32`, and `Int64`; omitted
input dtype means Float32. Unknown root, tensor, color, and feedback fields SHALL fail.

#### Scenario: Supported Model structure
- **WHEN** a Model descriptor follows the v1 matrix
- **THEN** it passes Model schema validation without loading its artifact

#### Scenario: Supported build-time download metadata
- **WHEN** a Model descriptor declares a complete canonical `hfDownload` object
- **THEN** schema validation accepts it without adding that metadata to runtime model loading

#### Scenario: Download destination escapes lexically
- **WHEN** a Model descriptor combines `hfDownload` with an absolute `modelFile` or a `modelFile`
  containing a parent-traversal component
- **THEN** Model schema validation rejects the descriptor

#### Scenario: Externally managed runtime model
- **WHEN** a Model descriptor has no `hfDownload` and uses an absolute or parent-traversing local
  `modelFile`
- **THEN** Model schema validation accepts the path for resolved runtime handling

#### Scenario: Legacy runtime locator
- **WHEN** `modelFile` contains an `hf:` locator or another URI-like value
- **THEN** Model schema validation rejects it before runtime path resolution

#### Scenario: Matrix violation
- **WHEN** an input/output kind, dtype, shape, or optional field combination is not supported
- **THEN** Model schema validation fails at that tensor

#### Scenario: Model number exceeds finite float range
- **WHEN** a mean/std component or Value/Vector input number cannot be represented as finite
  `float`
- **THEN** Model schema validation fails before typed projection

### Requirement: Model download schema gate

Before making any remote request, the model downloader SHALL load the checked-in
`config/schemas/v1/model.schema.json`. A JSON object containing either a root `modelFile` or
`hfDownload` member SHALL be identified for Model consumption and validated before the downloader
inspects either field. It SHALL prepare and validate all download destinations before starting the
first download. The downloader SHALL NOT duplicate schema-expressible lexical path rules in Python.
Expected schema, JSON, and filesystem failures SHALL be emitted once through Python's standard
logger at the executable boundary, followed by a nonzero exit without a traceback.

After schema success, the downloader SHALL resolve each `hfDownload` destination and reject it if
filesystem state places it outside the descriptor directory. This resolved containment check SHALL
cover symlink traversal that JSON Schema cannot observe. A valid descriptor without `hfDownload`
SHALL be ignored by artifact staging even when its runtime `modelFile` is absolute.

#### Scenario: Invalid descriptor blocks all downloads
- **WHEN** any consumed Model JSON violates the Model v1 schema
- **THEN** the downloader logs one concise validation error, exits nonzero without a traceback, and
  makes no Hugging Face request

#### Scenario: Download metadata omits its destination
- **WHEN** a JSON object declares `hfDownload` but omits `modelFile`
- **THEN** the downloader identifies it as a Model candidate and reports the schema failure before
  making a Hugging Face request

#### Scenario: Model schema contains an unresolved reference
- **WHEN** validation reaches a `$ref` that cannot be resolved in the checked-in Model schema
- **THEN** the downloader logs one concise schema error naming the reference, exits nonzero without
  a traceback, and makes no Hugging Face request

#### Scenario: Symlink escapes the model directory
- **WHEN** a schema-valid relative `modelFile` resolves outside its descriptor directory through a
  symlink
- **THEN** the downloader rejects the destination before making a Hugging Face request

#### Scenario: Valid external runtime path is not staged
- **WHEN** a schema-valid Model descriptor has an absolute `modelFile` and no `hfDownload`
- **THEN** the downloader performs no artifact request or destination containment check for it

### Requirement: Model v1 semantics

After schema success and without artifacts, Model v1 SHALL run these stable rules:

- `model.v1.tensor-size`: checked `size_t` multiplication of every declared shape SHALL not
  overflow, followed by checked multiplication by the effective dtype byte width;
- `model.v1.feedback-destination`: destination index SHALL exist, target RawTensorData, and occur at
  most once; source fan-out is allowed;
- `model.v1.feedback-source`: for static output, source index SHALL exist;
- `model.v1.feedback-compatible`: for static output, source and destination effective dtype and
  exact shape SHALL match.

Dynamic-output source existence and compatibility SHALL be deferred rather than guessed.

#### Scenario: Static compatible feedback
- **WHEN** a feedback source and destination exist and have identical dtype and shape
- **THEN** Model v1 semantics accept the feedback

#### Scenario: Duplicate feedback destination
- **WHEN** two structurally distinct feedback entries target the same input
- **THEN** `model.v1.feedback-destination` reports the later destination and relates it to the first

#### Scenario: Dynamic source metadata is unknown
- **WHEN** dynamic output prevents source existence or compatibility from being proven
- **THEN** descriptor-phase Model validation does not invent an artifact result

### Requirement: OpChain descriptor v1 schema

OpChain v1 SHALL require `version: 1`, canonical trim-nonempty `name` and
`description`, and a non-empty `ops` array. Each Op SHALL contain only `id`, `attributes`, and an
optional positive `loopId`; zero SHALL be represented by omission. `id` SHALL be exactly one
`library/op` pair with at least one non-whitespace character in each component. The removed `group`
field SHALL not be part of v1.

Known built-in Op attributes SHALL be closed:

| Op ID | Attributes |
|---|---|
| `pek-std-ops/InferenceController` | optional `contentType=""` |
| `pek-std-ops/GenericImagePreprocess` | optional trim-nonempty `inputImageSourceName="pipelineVideoFrame"` and optional integer `inputImageTensorIndex` in `0..15` |
| exact `<library>/Inference` | required trim-nonempty local-filesystem `modelDescriptor`, relative to the OpChain descriptor or absolute; URI-like values are rejected |
| `pek-std-ops/GenericPostprocess` | required parser and only that parser's attributes below |

GenericPostprocess parser attributes SHALL be:

| Parser | Optional fields and effective defaults |
|---|---|
| `CameraContactParser` | `contactClassIndex=1`, `noContactClassIndex=0`; each is 0 or 1 and only `(1,0)` / `(0,1)` is valid |
| `DummyParser` | `log=false` |
| `GazeDetectionParser`, `ObjectEmbeddingParser`, `PersonClassificationParser`, `RvmParser` | none |
| `ImageNetClassificationParser` | positive `topK=5`; `confidenceThreshold=0.01` in `[0,1]` |
| `ModNetSegmentationParser` | `thresholdLow=0.2`, `thresholdHigh=0.8`, each in `[0,1]` |
| `PaddleOcrDetectionParser` | `thresholdLow=0.6`, `thresholdHigh=0.8` in `[0,1]`; positive finite-float `gamma=0.5` |
| `ScrfdParser` | `confidenceThreshold=0.5`, `iouThreshold=0.4` in `[0,1]`; `normalizeOutputCoordinates=false`; positive `maxDetections=100` |
| `UltrafaceParser` | `confidenceThreshold=0.5`, `iouThreshold=0.3` in `[0,1]`; `normalizeOutputCoordinates=true` |
| `YoloParser` | Yolo v1 matrix below |
| `YoloXParser` | YoloX v1 matrix below |

Yolo v1 SHALL allow `outputFormat="UltraliticsYolo"` (`UltraliticsYolo` or `HailoYoloNMS`),
`confidenceThreshold=0.25` and `iouThreshold=0.45` in `[0,1]`,
`coordinatesAreNormalized=false`, `normalizeOutputCoordinates=true`, and `applyNms=true`.
Explicit `applyNms=false` SHALL prohibit `iouThreshold`. Only `HailoYoloNMS` SHALL allow positive
`maxDetections=5`, positive `classCount=80`, positive `maxBboxesPerClass=100`, and
`coordOrder="yxyx"` (`yxyx` or `xyxy`).

YoloX v1 SHALL allow positive `classCount=80`, `confidenceThreshold=0.25` and
`iouThreshold=0.45` in `[0,1]`, positive `maxDetections=100`, `applyNms=true`, `decoded=false`,
`scoresAreLogits=false`, `normalizeOutputCoordinates=true`, and
`scoreMode="objectnessClass"` (`objectnessClass` or `classOnly`). Explicit `applyNms=false` SHALL
prohibit `iouThreshold`.

Schema `default` values are annotations; the C++ consumers SHALL implement these effective
defaults and SHALL reject present wrong types rather than defaulting them. Custom Op attributes
SHALL remain open recursive JSON objects. Parser type/range rules, CameraContact class pairing,
Yolo/YoloX conditionals, and unused-key rejection SHALL be schema-owned.

The exact operation name `Inference` SHALL be reserved for the shared v1 inference extension
contract: its attributes SHALL be closed to `modelDescriptor`, and its runtime implementation SHALL
provide `OpInterfaceInference`. A new backend using that contract SHALL require no central backend
allowlist change. Plugin/factory loading, interface conformance, artifact validity, and backend
compatibility SHALL remain runtime checks. A differently named Op such as `InferenceLike` SHALL
remain a custom Op with open attributes.

#### Scenario: Known built-in attributes
- **WHEN** a built-in Op uses only its v1 attributes
- **THEN** it passes OpChain schema validation

#### Scenario: Unknown built-in attribute
- **WHEN** a built-in Op contains an attribute not consumed by its v1 implementation
- **THEN** OpChain schema validation fails at that attribute

#### Scenario: Custom Op attributes
- **WHEN** an unknown custom Op contains recursively nested JSON attributes
- **THEN** structural validation accepts the attribute object

#### Scenario: Parser number exceeds finite float range
- **WHEN** PaddleOCR `gamma` cannot be represented as finite `float`
- **THEN** OpChain schema validation fails before parser setup

### Requirement: OpChain v1 semantics

After schema success and before binding, OpChain v1 SHALL run these stable rules:

- `opchain.v1.op-id-control`: every Op ID is free of C0, DEL, and C1 control characters before
  plugin lookup;
- `opchain.v1.model-descriptor-control`: every exact `<library>/Inference` `modelDescriptor` is free of C0,
  DEL, and C1 control characters before filesystem use;
- `opchain.v1.loop-group`: every nonzero loop ID forms one contiguous group of at least two Ops;
- `opchain.v1.builtin-stage`: every GenericImagePreprocess has a successor; a
  controller-led built-in stage extends to the next controller or chain end and contains exactly
  one GenericImagePreprocess, exactly one immediately following exact `<library>/Inference`, and
  exactly one final GenericPostprocess, with zero or more custom Ops only between inference and the
  final postprocess;
- `opchain.v1.stage-loop`: a controller-led built-in stage is either entirely unlooped or every Op
  in the stage shares one nonzero loop ID beginning at its InferenceController; a non-empty
  controller `contentType` requires the looped form;
- `opchain.v1.threshold-order`: the effective `thresholdLow` is lower than effective
  `thresholdHigh` for ModNet and PaddleOCR detection parsers.

#### Scenario: Valid built-in crop stage
- **WHEN** a non-empty-content built-in stage is ordered correctly and shares one nonzero loop
- **THEN** OpChain v1 semantics accept it

#### Scenario: Looped controller does not start its group
- **WHEN** a built-in InferenceController has the same nonzero loop ID as its preceding Op
- **THEN** `opchain.v1.stage-loop` reports the controller independently of its `contentType`

#### Scenario: Partially looped built-in stage
- **WHEN** only some Ops in a controller-led built-in stage declare a loop ID
- **THEN** `opchain.v1.stage-loop` reports the inconsistent stage loop independently of the
  controller `contentType`

#### Scenario: Reused separated loop
- **WHEN** a loop ID occurs in two separated runs
- **THEN** `opchain.v1.loop-group` reports the later run

#### Scenario: Invalid threshold relation
- **WHEN** a supported low/high parser has effective low greater than or equal to effective high
- **THEN** `opchain.v1.threshold-order` reports the relation

#### Scenario: Control character at a runtime string boundary
- **WHEN** an Op ID or exact `<library>/Inference` `modelDescriptor` contains a C0, DEL, or C1 control
  character
- **THEN** the applicable OpChain v1 control rule rejects it before plugin or filesystem use

### Requirement: OpChain runtime scheduling

An `InferenceController` with its default empty `contentType` SHALL schedule one crop covering
`pipelineVideoFrame`. A non-empty `contentType` SHALL schedule one crop and parent UUID for every
existing `Perception::Rect` of that content type. `GenericImagePreprocess` SHALL consume one
scheduled crop per pass and return `BreakLoop` when none remain.

At runtime, a contiguous nonzero loop group SHALL execute its first Op once, then repeat the
remaining Ops. `BreakLoop` from any group member SHALL skip the group remainder and continue at
the next Op after that group; `AbortChain` SHALL stop the chain successfully.

#### Scenario: Full-frame controller
- **WHEN** an InferenceController omits `contentType`
- **THEN** its following stage processes one crop covering `pipelineVideoFrame`

#### Scenario: Detection-crop controller
- **WHEN** a looped InferenceController selects `humanFace`
- **THEN** its workers process one crop for each current `humanFace` rectangle and stop the loop
  when the scheduled crops are exhausted

#### Scenario: Loop worker exits
- **WHEN** a worker returns `BreakLoop`
- **THEN** the scheduler skips the remaining loop workers and resumes with the next Op after the
  group without re-running the group's first Op

### Requirement: Canonical typed projections

Model and OpChain typed projection SHALL occur within validation and no typed value SHALL be
published before parse, schema, and applicable descriptor semantics succeed. Projection failures
SHALL become structured validation issues rather than escaping exceptions. The public validation
result SHALL be an immutable typed validated value, not a mutable DOM. Serializers SHALL emit only
v1-valid fields, including `version`, without a redundant type field.

Every schema-valid number projected to or consumed as `float` SHALL remain finite after projection,
and canonical serialization of a validated typed descriptor SHALL validate again as the same v1
descriptor type before any file-backed runtime path resolution.

The OpChain projection SHALL include description and contain neither a group field nor a zero loop
sentinel. Its optional `loopId` SHALL project omission as no value, not as zero. Only the
descriptor-to-runtime handoff SHALL convert no value to the scheduler's private zero sentinel, and
a manually constructed descriptor containing an explicit zero SHALL fail typed semantic validation.

Build-only `hfDownload` SHALL be omitted from the validated JSON projection and its canonical
serialization. `ModelDescriptor::fromJson` SHALL expose the validated authored `modelFile` value.
`ModelDescriptor::fromFile` SHALL keep its existing return type and resolve a relative `modelFile`
from the descriptor directory in the returned runtime value while preserving an absolute path.
Backends SHALL continue using that resolved `modelFile` directly; no second loaded wrapper or
backend interface SHALL be introduced.

`OpChainDescriptor::fromJson` SHALL preserve authored `modelDescriptor` values for canonical
serialization. `OpChainDescriptor::fromFile` SHALL resolve relative exact `<library>/Inference` references
from the OpChain descriptor directory and preserve absolute references before configuring Ops.
Neither file-backed loader SHALL lexically normalize or canonicalize the joined path. Filesystem
components SHALL retain their authored order so the operating system resolves symlinks followed by
parent traversal without silently selecting a different target. Target canonicalization and
symlink-policy validation remain resolved/runtime concerns.

#### Scenario: Canonical OpChain round trip
- **WHEN** a valid OpChain without a loop is serialized
- **THEN** the typed descriptor contains no loop value and the result contains version/description
  and omits `loopId`

#### Scenario: File-backed Model uses a local artifact
- **WHEN** a valid Model descriptor is loaded from `config/models/example/model.json`
- **THEN** the returned `ModelDescriptor::modelFile` resolves below that descriptor directory and
  backends consume it through the existing interface

#### Scenario: File-backed OpChain resolves a model descriptor
- **WHEN** an OpChain descriptor contains `../../models/example/model.json`
- **THEN** file-backed loading resolves it from that OpChain descriptor's directory before the
  Inference Op is configured

#### Scenario: Absolute local reference
- **WHEN** `modelFile` or an exact `<library>/Inference` `modelDescriptor` contains an absolute filesystem path
- **THEN** validation accepts it and file-backed loading preserves its absolute meaning

#### Scenario: Symlink followed by parent traversal
- **WHEN** a valid relative or absolute reference contains a symlink component followed by `..`
- **THEN** file-backed loading preserves those components for normal filesystem resolution instead
  of lexically rewriting the path to a different target

#### Scenario: URI reference
- **WHEN** either filesystem reference contains an `hf:`, `file:`, or other URI-like value
- **THEN** schema validation rejects it before runtime path resolution

#### Scenario: Direct typed OpChain setup
- **WHEN** a caller supplies a manually constructed OpChain descriptor to `setupFromDescriptor`
- **THEN** the shared typed OpChain semantics run before any Op is bound or configured

#### Scenario: Explicit typed zero loop is rejected
- **WHEN** a manually constructed OpChain descriptor contains an engaged `loopId` equal to zero
- **THEN** typed OpChain semantics reject it before conversion to the runtime representation

### Requirement: Repository CLI and CI

`pek-config-check --root <repo-root> --format text|json` SHALL parse with duplicate detection and
meta-validate the complete current checked-in schema bundle, discover every JSON descriptor below
`config/models/` and `config/opchains/`, exclude `config/experimental/`, validate the complete set,
and report all independently readable failures deterministically. Repository discovery SHALL
inspect routing filenames rather than descriptor content. Traversal, live-schema loading, and
aggregation SHALL be private CLI implementation details and SHALL add no cross-descriptor semantic
rules.

JSON output SHALL contain `diagnosticFormatVersion: 1` and the shared issue fields. Exit SHALL be 0
for valid, 1 for validation failure, and 2 for invocation/internal failure.
`expkits-ci --config-schema-check` SHALL only invoke this CLI, and PR/full presets SHALL include it.
Help, successful reports, and every JSON report SHALL be written to stdout without logger prefixes.
Human-readable validation failures and invocation/internal diagnostics SHALL use
`pek::log::error()` and SHALL be flushed before exit so they reach the configured PEK log targets.
JSON reports are the machine-readable diagnostic interface and SHALL NOT be duplicated into the
logger.

#### Scenario: Complete supported repository
- **WHEN** the CLI validates the migrated checkout
- **THEN** the live schema bundle and every discovered supported descriptor pass their applicable
  schema and per-document validation

#### Scenario: Supported models without runnable OpChains
- **WHEN** PaddleOCR recognition has a canonical `model-<variant>.json` descriptor but no runnable
  OpChain
- **THEN** `pek-config-check` treats it exactly like every other supported Model descriptor without
  requiring a placeholder OpChain or a discovery exception

#### Scenario: Live schema edit in a running container
- **WHEN** a contributor edits a checked-in schema and runs `pek-config-check`
- **THEN** the current schema is meta-validated and used by the same C++ engine

#### Scenario: Stable machine-readable diagnostics
- **WHEN** validation fails in JSON format
- **THEN** output version 1 orders issues by file, phase (`parse`, `dispatch`, `schema`,
  `descriptor`), instance location, and rule

#### Scenario: Machine-readable output remains clean
- **WHEN** a valid or invalid repository is requested in JSON format
- **THEN** stdout contains one parseable report without logger prefixes or a duplicate log record

#### Scenario: Human-readable validation failure uses standard logging
- **WHEN** repository validation fails in text format
- **THEN** the CLI emits the report once through `pek::log::error()`, flushes it, and exits 1

### Requirement: Contributor documentation

The bring-your-model and architecture documentation SHALL show minimal v1 Model and OpChain
examples, the dev-container validation command, built-in versus custom attribute ownership, the
scheduler behavior that executes a loop's first Op once before repeating its workers, and the
boundary between artifact-free descriptor validation and resolved/runtime checks.

#### Scenario: New contributor validates a descriptor
- **WHEN** a contributor follows the documented workflow
- **THEN** they can run the same CLI validation used by CI before trying a pipeline
