# Config Descriptor Validation v1 Specification

## Purpose

Define and enforce the first supported, artifact-free Model and OpChain descriptor contract across
production loading, repository tooling, and CI.

## Requirements

### Requirement: One versioned validation pipeline

Every supported Model and OpChain descriptor SHALL pass duplicate-aware JSON parsing,
filename/version routing, offline Draft 2020-12 schema validation, and its applicable v1 semantic
rules before model-file use or Op binding. Production loading and repository validation SHALL
produce the same per-document result, and a failed operation SHALL be reported once.

Every supported descriptor SHALL contain `version` equal to 1. `model.json` and
`model-<variant>.json` filenames SHALL select Model validation; `opchain.json` and
`opchain-<variant>.json` filenames SHALL select OpChain validation, where `<variant>` contains at
least one character. The basename SHALL end exactly in `.json`; `model-.json` and
`opchain-.json` are unsupported. Schema selection SHALL use only this filename convention
and `version`, without a model-specific exception. File-backed validation SHALL route the actual
path. In-memory Model and OpChain validation SHALL route as `model.json` and `opchain.json`
respectively unless an explicit source path is supplied. The supported schemas SHALL be:

- `config/schemas/v1/model.schema.json`,
  `$id: urn:arm:pek:schema:model-descriptor:v1`;
- `config/schemas/v1/opchain.schema.json`,
  `$id: urn:arm:pek:schema:opchain-descriptor:v1`.

The OpChain root schema SHALL reference local versioned resources under
`config/schemas/v1/opchain/`. Those resources SHALL describe the shared Op structure and each
supported built-in Op's ID selection and attribute contract. The `GenericPostprocess` resource
SHALL own parser variants as local `$defs`; variants with the same closed attribute shape MAY share
one definition. A parser definition SHALL become a standalone resource only when a second schema
consumer requires it. The exact `<library>/Inference` ID shape SHALL select one shared Inference
resource without enumerating backend libraries.

Every schema resource SHALL be valid against the built-in Draft 2020-12 meta-schema, declare a
unique `$id`, and resolve every local reference from the checked-in bundle without network access.

#### Scenario: Model v1 routing
- **WHEN** `model.json` or `model-<variant>.json` declares `version: 1`
- **THEN** the common and Model v1 validation layers run

#### Scenario: OpChain v1 routing
- **WHEN** `opchain.json` or `opchain-<variant>.json` declares `version: 1`
- **THEN** the common and OpChain v1 validation layers run

#### Scenario: Unsupported filename or version
- **WHEN** the descriptor filename is unsupported or `version` is missing or unsupported
- **THEN** validation fails before descriptor use or setup

#### Scenario: Production filename determines type
- **WHEN** a Model payload is loaded from a filename outside `model.json` and
  `model-<variant>.json`
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

#### Scenario: Broken schema resource
- **WHEN** a v1 schema is meta-invalid, duplicates another resource's `$id`, or references an
  unavailable local target
- **THEN** the repository-native schema-resource check fails without network resolution

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

Model validation SHALL reject C0, DEL, and C1 control characters in `modelFile` before filesystem
or model-library use.

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

Every JSON number consumed as a runtime `float` SHALL lie in the inclusive finite range
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
- **THEN** Model schema validation fails before runtime use

### Requirement: Model download schema gate

Before making any remote request, the model downloader SHALL load the checked-in
`config/schemas/v1/model.schema.json`. A JSON object containing either a root `modelFile` or
`hfDownload` member SHALL be identified for Model consumption and validated before the downloader
inspects either field. It SHALL prepare and validate all download destinations before starting the
first download. Expected schema, JSON, and filesystem failures SHALL produce one concise
diagnostic, a nonzero exit, and no traceback.

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

- `model.v1.tensor-size`: every declared shape and its effective dtype byte width SHALL produce a
  representable total byte size;
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
`description`, and a non-empty `ops` array. Each Op SHALL contain `id`, optional `attributes`, and
optional positive `loopId`; zero SHALL be represented by omission. `id` SHALL be exactly one
`library/op` pair with at least one non-whitespace character in each component. The removed `group`
field SHALL not be part of v1. Exact `<library>/Inference` and
`pek-std-ops/GenericPostprocess` Ops SHALL require `attributes`; built-in controller and preprocess
Ops and custom Ops MAY omit it, with omission equivalent to an empty object.

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

Yolo v1 SHALL allow `outputFormat="UltralyticsYolo"` (`UltralyticsYolo` or `HailoYoloNMS`),
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

Omitted fields SHALL use the listed effective defaults. Present fields with unsupported types
SHALL be rejected instead of replaced by defaults. Custom Op attributes SHALL remain open
recursive JSON objects. Parser type/range rules, CameraContact class pairing, Yolo/YoloX
conditionals, and unused-key rejection SHALL be enforced during schema validation.

The exact operation name `Inference` SHALL be reserved for the shared v1 inference extension
contract: its attributes SHALL be closed to `modelDescriptor` regardless of library prefix. A new
backend using that contract SHALL be accepted without changing the descriptor contract. Plugin
loading, interface conformance, artifact validity, and backend compatibility SHALL remain runtime
checks. A differently named Op such as `InferenceLike` SHALL remain a custom Op with open
attributes.

#### Scenario: Known built-in attributes
- **WHEN** a built-in Op uses only its v1 attributes
- **THEN** it passes OpChain schema validation

#### Scenario: Op without configuration
- **WHEN** a controller, preprocess, or custom Op omits `attributes`
- **THEN** OpChain schema validation treats the Op as having an empty attribute object

#### Scenario: Configured Op requires attributes
- **WHEN** an exact `<library>/Inference` or `pek-std-ops/GenericPostprocess` Op omits `attributes`
- **THEN** OpChain schema validation rejects the Op

#### Scenario: Unknown built-in attribute
- **WHEN** a built-in Op contains an attribute not declared by the v1 contract
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

### Requirement: Validated descriptor values and path resolution

`ModelDescriptor` and `OpChainDescriptor` runtime projection entry points SHALL return typed values
only after parsing, schema validation, all applicable descriptor semantics, and conversion succeed.
This covers their `fromJson` and `fromFile` entry points. A conversion failure SHALL be reported as
a validation issue. Canonical serialization of those projections SHALL emit only v1 fields,
including `version` and no redundant type field, and the serialized result SHALL validate again as
the same descriptor type.

Every schema-valid number consumed as a runtime `float` SHALL remain finite. A serialized OpChain
SHALL include `description`, omit the removed `group` field, and omit `loopId` when no loop is
authored. An explicitly present zero loop SHALL fail validation.

Build-only `hfDownload` SHALL not be present in the typed `ModelDescriptor` runtime projection or
emitted by its canonical serialization. In-memory validation SHALL preserve authored `modelFile`
and `modelDescriptor` values. File-backed loading SHALL resolve relative references from the
containing descriptor's directory before runtime use and preserve absolute references.

File-backed path resolution SHALL preserve authored components instead of lexically normalizing or
canonicalizing the joined path. The operating system therefore remains responsible for resolving a
symlink followed by parent traversal; target canonicalization and symlink policy remain runtime
concerns.

#### Scenario: Canonical OpChain round trip
- **WHEN** a valid OpChain without a loop is serialized
- **THEN** the result contains version and description, omits `loopId`, and validates as OpChain v1

#### Scenario: File-backed Model uses a local artifact
- **WHEN** a valid Model descriptor is loaded from `config/models/example/model.json`
- **THEN** its relative `modelFile` resolves below that descriptor directory before backend use

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

#### Scenario: Direct in-memory OpChain setup
- **WHEN** a caller supplies an in-memory OpChain descriptor for setup
- **THEN** OpChain semantics run before any Op is bound or configured

#### Scenario: Explicit zero loop is rejected
- **WHEN** an in-memory OpChain descriptor contains `loopId` equal to zero
- **THEN** validation rejects it before runtime scheduling

### Requirement: Repository CLI and CI

`pek-config-check --root <repo-root>` SHALL parse with duplicate detection and
meta-validate the complete current checked-in schema bundle, discover every JSON descriptor below
`config/models/` and `config/opchains/`, exclude `config/experimental/`, validate the complete set,
and report all independently readable failures deterministically. Repository discovery SHALL
inspect routing filenames rather than descriptor content and SHALL add no cross-descriptor
semantic rules.

Validation failures SHALL be emitted once in a human-readable report ordered by file, phase
(`parse`, `dispatch`, `schema`, `descriptor`), instance location, and rule. Exit SHALL be 0 for
valid, 1 for validation failure, and 2 for invocation/internal failure.
`expkits-ci --config-schema-check` SHALL only invoke this CLI, and PR/full presets SHALL include it.
Help and successful reports SHALL be written to stdout; every failure SHALL be made visible before
exit.

#### Scenario: Complete supported repository
- **WHEN** the CLI validates the supported checkout
- **THEN** the live schema bundle and every discovered supported descriptor pass their applicable
  schema and per-document validation

#### Scenario: Supported models without runnable OpChains
- **WHEN** PaddleOCR recognition has a canonical `model-<variant>.json` descriptor but no runnable
  OpChain
- **THEN** its concrete input is `[1,3,48,640]`, matching the ONNX input's fixed height and a valid
  dynamic width
- **AND** `pek-config-check` treats it exactly like every other supported Model descriptor without
  requiring a placeholder OpChain or a discovery exception

#### Scenario: Live schema edit in a running container
- **WHEN** a contributor edits a checked-in schema and runs `pek-config-check`
- **THEN** the current schema is meta-validated and used for descriptor validation

#### Scenario: Human-readable validation failure
- **WHEN** repository validation fails
- **THEN** the CLI emits the report once and exits 1

### Requirement: Contributor documentation

The bring-your-model and architecture documentation SHALL show minimal v1 Model and OpChain
examples, the dev-container validation command, built-in versus custom attribute ownership, the
scheduler behavior that executes a loop's first Op once before repeating its workers, and the
boundary between artifact-free descriptor validation and resolved/runtime checks.

#### Scenario: New contributor validates a descriptor
- **WHEN** a contributor follows the documented workflow
- **THEN** they can run the same CLI validation used by CI before trying a pipeline
