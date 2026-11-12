---
sidebar_position: 10
sidebar_label: FrameResults schema
---
<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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


# FrameResults schema

The `open_perception_kit.metadata` schema defines structured runtime results.
`open_perception_kit::FrameResults` is the concrete C++ runtime container: a generated,
typed envelope that travels downstream with a media buffer and accumulates
payloads from postprocessing, tracking, and performance elements.

The public SDK packages and C++ facade use **open-perception-kit** names;
`open_perception_kit` is the packet producer identity.
**FrameResults** identifies one frame's runtime result envelope.

## Generated SDKs

The canonical schema and generated output directories are declared in
`tools/perception/sdk.json`. Run `./scripts/perception-sdk.sh generate` to
regenerate the checked-in C++, Python, Rust, and TypeScript SDKs; use
`./scripts/perception-sdk.sh check` in CI to detect drift.

Development generation and release packaging are intentionally separate.
`$regenerate-perception-sdk` updates tracked generated sources during
implementation. `$package-open-perception-kit-release` consumes an already committed
snapshot and creates distributable artifacts without regenerating it.

`tools/perception/sdk.json` is the only hand-edited SDK release descriptor. It
defines the wire identity, public package name, canonical schema and generated directories,
flowdata-sdk location, generated project integrations, and checksum-locked
FlatBuffers and Python wheel-build artifacts. The SDK packages share the OPK
product version from `development/meson.build`. `tools/perception/sdk_config.py` is the shared loader
used by generation, packaging, tests, and development installation. Generation first verifies the raw
flowdata manifests, then applies OPK-owned copyright and formatting decoration. The final
generated SDK manifest embeds the raw generator manifests and records the
descriptor hash, decorated file hashes, and derived project integrations.
Generation also refreshes the exact Plumber SDK dependency and the checked-in
WebUI bundle that consumes the TypeScript SDK; `check` verifies both.

The Flowdata generator sources are normal tracked files under
`tools/flowdata-sdk`. Updates are manual source changes, reviewed together with
regenerated SDK outputs. Generation receipts identify the local generator
content by hashes; no separate Git checkout, remote reference, or automated
fetch/update step is required.

The generated Python package exposes endpoint ownership through
`open_perception_kit.packet` and live C++ guest access through `open_perception_kit.guest`. The
generated internal Meson adapter is derived from the public SDK integration and
carries the same version requirements.

Run `./scripts/perception-sdk.sh package --output-dir artifacts` to create a
reproducible release archive containing the C++ SDK and integrations, open-perception-kit
and FlatBuffers Python wheels, schemas, and release manifest. See
[Build and use the Open Perception Kit bundle](../public/how-to/use-perception-sdk.md)
for the archive layout and consumer workflow.

Release packaging verifies the checked-in generation receipt and never invokes
the generator. Regeneration drift remains an explicit
`./scripts/perception-sdk.sh check` responsibility for development and CI.

All SDK operations use `./scripts/perception-sdk.sh` as their single command
surface.

See `schemas/perception/README.md` for schema versioning, FlatBuffers
compatibility rules, new payload creation, and the required validation
workflow.

## Runtime Envelope

`FrameResults` can contain multiple independent typed payloads. Producers append
payloads with the generated `add()` API, and consumers select known payload
families with `for_each<T>()`. This allows a frame to accumulate results across
cascades and independent elements without relying on layer ordering or a
hand-written variant container.

The currently generated payload roots are:

- `FrameContext` for source-frame dimensions and timing
- `BoxDetections` for localized detections
- `Classifications` for ordered classification candidates with optional label-set identity
- `CameraContacts` for explicit contact decisions and optional diagnostic model indices
- `PersonPresences` for person/no-person scores
- `GazeMeasurements` for degree-angle pairs and explicit subject references
- `SegmentationMasks` for compositing masks and alpha mattes
- `SemanticSegmentationMasks` for per-pixel class maps
- `InstanceSegmentationMasks` for individual object masks with class, confidence, and box
- `ObjectEmbeddings` for embedding vectors
- `ObjectTracks` for tracker output and predicted detections
- `TrackTraces` for tracker history points
- `PerformanceMeasurements` for numeric scope timings and pipeline frame rate

Each payload family has a stable identity derived from its fully qualified root
name. Compatible field additions preserve that identity. Consumers accept matching
SDK names and major versions before typed interpretation; schema digests describe
the producer's build but do not determine compatibility. Unknown payload entries
can be forwarded without typed access. SDK 1.0 uses FlowData v0.7.1 identities;
producers and consumers must upgrade together from the earlier ID scheme.

## Shared Metadata

Payloads that represent inference or processing output carry `LayerInfo`. It
records interpretation and provenance fields such as engine, model, tags,
producer element ID, label family, content type, and compositing mode. Consumers
must select payloads by type and semantic fields such as `content_type`, not by
their position in the envelope.

`LayerInfo.producer` identifies the component that created the payload rather
than only the enclosing inference element. Its `instance_id` distinguishes
multiple operations in one OpChain, `component` identifies the canonical Op or
element type, and `implementation` identifies the selected parser, script, or
processing implementation. Producer metadata is provenance and must not be
used as a replacement for payload-type or content-type routing.

Result items use `ObjectMeta` where identity or parent relationships are needed:

- `id` identifies an item within the producer's result model
- `parent_id` links derived output to its source item; it is not a coordinate transform
- `creation_ts_ns` records producer creation time in Unix-epoch nanoseconds, not media PTS

### Image geometry

Box detections and tracks carry optional unsigned category IDs. Absence means no
category identity; zero is a valid category and is preserved through tracking,
SDKs and consumers. The browser preserves non-finite confidence values for
validation instead of turning them into a zero score.

`FrameContext.video` records the full source image dimensions and optional
`presentation_ts_ns` from the media backend. An unavailable presentation time is
absent; zero is valid. The runtime registers a source image once per OpChain
execution. Its object ID identifies that frame context, not a persistent track
or a globally unique identity across producers.

Each `BoundingBox` carries an `ImageSpace` and `CoordinateUnits`. The space embeds
the source frame ID, full width/height, and crop origin/extent in source-frame
pixels. Coordinates are top-left and size, expressed either in crop-local pixels
or fractions of that crop's extent. For normalized coordinates:

```text
frame_x = origin_x + x * extent_width
frame_y = origin_y + y * extent_height
frame_width = width * extent_width
frame_height = height * extent_height
```

Pixel coordinates use the same translation without the extent multiplication.
Consumers scale these source-frame pixels to the target surface dimensions.
The description is self-contained: cached tracker predictions can retain their
coordinate basis without retaining the original frame observation. Parent IDs
continue to express derivation, independently of this spatial reference.

Parsers undo model resizing/letterboxing before publishing crop-local boxes.
CPU, Vulkan and PaddleOCR preprocessing supply the actual source and crop geometry. The former unused
crop/letterbox fields on `VideoFrameContext` are removed; preprocessing padding
is not a property of the full source frame.

Missing spaces, zero-sized or out-of-frame extents, unknown units, and non-finite
coordinates are not guessed from a parent or the first frame in a packet. Native
and browser renderers omit unplaceable geometry, the browser reports it as
unavailable, and Plumber reports an invalid spatial reference. Tracking operates
on owned pixel copies in the full-frame basis established by its first valid
detection, until reset. Incoming resolution changes are projected into that
basis without changing Kalman units or mixing trace coordinates. The detector's
original output is preserved; trace points carry their own image basis.
Plumber normalizes geometry using that
basis rather than the derivation parent's dimensions.

SDK 1.0 intentionally changes the interpretation of previously misrendered
normalized/crop-local boxes and comparisons. Correct full-frame pixel pipelines
retain their placement; the same generated snapshot is required throughout.

## Shared routing and result availability

Model capabilities and dependencies use explicit payload roots plus optional
roles, not content-type strings alone. The root is the generated fully qualified
SDK name; a role narrows selection within that root. Native parsers declare the
roots they publish, and trusted Python Ops can declare their inputs/outputs.
An empty declaration list means no declared capability; it is not evidence that
a script cannot publish data. Auxiliary source/performance metadata is separate
from model-result capability advertising.

Availability is represented by existing payload structure, without a guessed
shared score or negative result:

- A successfully executed collection producer publishes its typed payload even
  when the collection is empty. Detector threshold/NMS filtering can produce a
  valid empty `BoxDetections` payload.
- An absent payload is not an empty success: it may be disabled, unscheduled,
  gated, lacking eligible subjects, or unavailable. Skipped turn assessments do
  not become negative turn decisions.
- Missing optional measurements remain absent. Invalid records are not converted
  into valid zeros, false decisions, or empty collections. Processing errors remain
  errors rather than synthetic successes.
- An explicit classification kind/namespace with an empty candidate list is an
  empty result for its subject; unspecified kind/identity is not interchangeable.
- Unsupported or malformed payload entries are preserved by generic transport,
  reported as unhandled by browser inspection, and fail Plumber comparison unless
  a comparator explicitly supports them. Performance diagnostics remain explicitly
  excluded from semantic comparison.

Browser adapters expose `available`, `empty` and `invalid` layer states and a
count of unhandled entries. Invalid selected outputs are not rendered as empty
successes. Plumber accounts for every payload entry, including empty collections;
unknown protocols and malformed records cannot silently disappear into a match.

## Tracking observations and timed history

`ObjectTrack.object.id` identifies one published observation. Persistent identity
is the complete `TrackIdentity` tuple `(tracker_id, session_id, track_id)`, shared
by its `TrackTrace`. The local integer retains its familiar display value; it is
not globally unique. Each tracker gets an opaque UUID session token, renewed on
reset/restart, so independent trackers and reused numbers cannot alias. Layer
provenance and display labels are not substitutes for this explicit identity.

Every track observation and trace point carries a `TrackingSample`:

- `frame_index` is the one-based count of frames processed by this tracker session,
  not a packet object ID or the upstream frame counter.
- `elapsed_ms` is monotonic tracker-local elapsed time, not UTC or media PTS.
  Positive media running-time deltas advance it when available; otherwise the
  configured fallback interval does. The first increment uses fallback.
- `time_source` identifies `MediaDelta` versus `FallbackDelta` for that increment.
  Forcing Kalman fallback does not turn a valid lifecycle media delta into an
  estimated delta.
- `state` explicitly declares `Observed` or `Predicted`; unspecified states are
  not interpreted. Kalman prediction used internally before correction does not
  make a matched observation a predicted-only output.

`source_id` and `source_frame_index` are optional together. For observed output,
the source is in the current tracker frame. For predicted output, its source
frame is strictly earlier and the original observation need not be in the packet.
The box is the current tracker estimate, whereas `source_confidence`, optional
category ID and original label are retained from the source detection; they are
not new measurements or prediction-quality scores.

Observed association diagnostics declare `New`, `IoUScore`, `CosineScore` or
`Reacquired` and an optional score with defined units. The kind reports the
available diagnostic, not the configured matching strategy: a cosine score can
be reported during IoU or hybrid matching, as before. A prediction has no new association.
Tracking data no longer stores diagnostic strings or adds prediction tags to an
entire payload. `text` stays the original label. `ObjectTracks.display_identity`
controls presentation; renderers reconstruct the existing `ID:n`, `IOU`, `REID`,
`REID-R`, `NEW` and `PRED` labels, including two-decimal score formatting. The
existing `append-track-id-to-text` property controls this display preference.

Trace points are filtered/estimated box centres in their self-contained image
basis, ordered oldest to newest with strictly increasing frame indices and elapsed
times. Each point declares whether it was observed or predicted. The trace is the
complete current segment, not a delta update. Dormant reacquisition keeps the track
identity but starts a fresh segment, preserving the existing visual discontinuity.
History is retained by elapsed age, not a point count inferred from the latest
frame interval. This corrects variable-rate and forced-Kalman-fallback retention;
ordinary fixed-rate geometry, palettes and line styles remain unchanged.

Native/browser consumers join trails by the complete identity and reject ambiguous
duplicates. A trail may stand alone when predicted boxes are disabled. If a current
track accompanies it, its last sample must agree with that observation. Renderers
do not guess missing identity, chronology or spatial information.

Plumber compares tracker/local track numbers and sample state/time, validates
current source correspondence, and permits missing historical source observations
for predictions. Session UUID text differs between independent executions; one
unambiguous session per tracker in each packet establishes the comparison scope.
Multiple sessions without a stable correspondence fail rather than being guessed.
Trace lengths and sample chronology must agree; extra points and malformed data
are no longer silently accepted by comparing only a shared prefix. Display wording
and association score precision do not define track identity.

This is a breaking SDK 1.0 development migration of `ObjectTracks` and
`TrackTraces`, with unchanged root identities and regenerated bindings. No legacy
text/tag parser or old-point adapter is provided.

## Embedding spaces and selection

`ObjectEmbedding.space` declares a versioned opaque space ID, dimension and
comparison metric. The supported metric is `Cosine`; `Unspecified` or an unknown
metric is not usable. Equal vector lengths or producer/model display names do
not establish comparability. A producer may share a space ID with another
producer only when their vectors have the same meaning. Change the ID when
weights, preprocessing, output selection or vector semantics change incompatibly.
Dimension and metric must also agree before comparison.

`subject_id` explicitly names the observation described by the vector. It is
independent of `ObjectMeta.parent_id`, which remains derivation metadata.
Tracker selection resolves a unique box/track observation; it never substitutes
a persistent track ID or parent link. Plumber also supports unambiguous whole-frame
subjects through the shared subject resolver.

Vectors must have the declared dimension, finite values and nonzero norm to be
used. Missing vectors, zero vectors, unidentified spaces and unsupported metrics
are unavailable, not zero similarity. Cosine comparison does not assume unit
length. Native arithmetic accumulates Float32 values in double precision to avoid
overflow/underflow. `ObjectEmbeddingParser` retains its L2 normalization for valid
nonzero output, preserves zero vectors as unusable, and rejects malformed tensors
or non-finite output before publication.

The generic parser's optional `embeddingSpaceId` attribute supplies the semantic
space declaration. Without it, vectors remain transportable but cannot be used
for matching; a model name is not used as an identity fallback. This attribute
is introduced by the OpChain v1 reader schema `1.9.0`.

The tracker replaces `embedding-content-type` with optional `embedding-space-id`
and `embedding-producer-id` selectors. Empty selectors accept only a single
eligible candidate per subject. Duplicates are counted before vector validation,
so even a valid/invalid pair is ambiguous. Multiple spaces or producers never
overwrite one another by arrival order. A producer filter selects an exact
instance ID; producer provenance alone never establishes vector comparability.

Active and dormant track history retain the full typed embedding, including its
space. Incompatible/unavailable embeddings cannot contribute to association or
dormant reassociation. Hybrid mode retains IoU fallback, embedding-only mode
requires a valid compatible comparison, and existing thresholds/costs are
unchanged. Missing observations retain the last usable embedding; a later match
with a usable vector updates both values and space together.

Plumber retains all observations rather than collecting one vector per object.
It checks space, vector usability and corresponding subject source/geometry before
applying the existing clipped cosine distance `clamp(1 - cosine, 0, 1)` with
tolerance 0.2. Reordering equivalent outputs can match; changing spaces, swapping
subjects or dropping an output cannot. Browser decoding and terminal inspection
expose space, subject and usability without inventing defaults.

This is an intentional semantic/API migration within unreleased SDK 1.0. Old
unidentified embeddings must be given an explicit producer-authored space, and
users of the former tracker property must adopt the new selectors. No legacy
property alias or guessed embedding-space identity is provided.

## Mask representation

The existing mask roots declare separate data contracts. Rendering settings,
`content_type`, `label_family`, and derivation parents do not select their meaning.

| Root | Samples and meaning |
| --- | --- |
| `SegmentationMasks` | `Uint8` opacity with explicit `BackgroundOpacity` or `RegionOpacity`. Zero is no coverage, 255 full coverage, and intermediate values partial coverage. `Unspecified` is not interpretable. |
| `SemanticSegmentationMasks` | `Uint8` or `Uint32LE` category IDs in a non-empty `label_set`. Zero is a valid class. Only a present `void_value` reserves an unclassified value; it must fit the encoding. |
| `InstanceSegmentationMasks` | `Uint8` membership: 0 outside and 1 inside one observation. Other values are invalid. Instances may overlap. Optional unsigned `class_id` requires a label set when present; optional confidence must be finite in [0,1]. |

`BitmapData.encoding` replaces the free-form value-type string. Samples are
tightly packed row-major values, with no stride, color packing or compression;
multi-byte values are explicitly little-endian. Dimensions must be positive and
the byte length must equal width × height × bytes per sample exactly. Empty
results use an empty mask list, not a zero-sized bitmap. An all-zero bitmap is
valid and retains its family's meaning.

Every bitmap carries its own `ImageSpace`. The entire grid spans that region:
sample `(x,y)` covers `[x/width,(x+1)/width)` and `[y/height,(y+1)/height)` of its
extent. Grid resolution is independent of source-frame resolution. Consumers
project that region into the target surface and leave the surrounding image alone;
they do not stretch an ROI over the frame or find placement through `parent_id`.
An instance's optional box is separate detection geometry and must refer to the
same source basis as its bitmap.

MODNet and RVM publish background opacity; the older OCR mask parser publishes
region opacity after its threshold/gamma transform, not calibrated probabilities.
Their model-grid letterbox is removed before publication, retaining its sampling
density and mapping the active grid to the declared image region. YOLO semantic
and instance parsers already restore their grids to source pixels; they declare
`opk:cityscapes-19` and `opk:coco-80` respectively. Their full-frame-only inference
restriction remains, but validity no longer depends on a derivation-parent lookup.

The native opacity compositor retains its existing tint and binary background
threshold of 150; the browser background feature retains graded alpha and its
existing style controls. These are visualization policies, not transport meanings.
The browser decoder and terminal inspection support all mask roots. Semantic and
instance overlays use these declared image regions and label identities, with
per-class controls, explicit void handling and optional instance annotations.
See [the WebUI contract](../../development/web/README.md#semantic-segmentation).

Plumber validates and compares all mask families, including malformed-reference
failures. It checks normalized image regions at the existing geometry tolerance,
explicit meanings and category namespaces, then compares opacity by mean absolute
error / 255, class maps by unequal-sample fraction, and instances by IoU distance.
The pixel-distance tolerance is 0.1. Different grid resolutions use nearest
samples on the larger existing grid; category IDs are never interpolated.
Multiple source-frame IDs in one mask producer group are ambiguous for cross-packet
comparison and fail rather than guessing correspondence from local IDs.

This is an intentional breaking API/wire change within the unreleased SDK 1.0
refactor. All participants use the same generated snapshot; no old bitmap adapter
is supplied. Root identities remain stable while the shared schema digest changes.

## Classification representation

`Classification.label_set` names the vocabulary in which candidate `class_id`
values have meaning. The built-in ImageNet parser uses `opk:imagenet-1001`, the
checked-in index order including background. Categorical comparison requires a
non-empty label set and a present class ID; display text is not identity.
Class IDs are optional unsigned values: zero is valid, absence means no identity.
`Classification.kind` explicitly selects `Categorical` or `RecognizedText`;
`Unspecified` establishes neither. Recognized text requires an empty label set
and absent class IDs, and is compared literally. Missing identity fields never
change a categorical result into recognized text. Candidate order is the existing
producer ranking. The former `x/y/w/h` fields used for display rank are removed.
Candidate `text` remains unchanged, including recognized text and Unicode; text
without a declared vocabulary is not silently turned into a category identity.

Candidate confidence is optional; a supplied value must be finite and in `[0,1]`.
Consumers preserve invalid values for validation rather than converting them to
zero. The ImageNet parser reports an error for non-finite logits or invalid softmax
normalization, independently of valid predictions filtered by its threshold.

Contact and presence observations have their own payload roots. A present
`CameraContactResult.in_contact` directly states the outcome. Its optional unsigned
model index is diagnostic only: consumers never reinterpret it using model-specific
indices. The parser retains its existing configured contact/no-contact mapping.
An absent result means unavailable; a present false outcome means no contact.

`PersonPresenceResult.person_present` states the decision. Optional `yes_score`
and `no_score` are raw model outputs, both finite or both absent, not probabilities.
The existing binary parser preserves score ordering: larger yes-score means person;
ties mean no person. It does not infer logits versus probabilities or normalize
unknown values. Non-finite model scores produce an absent result. This changes the
metadata contract without changing the OpChain input configuration contract.

All three result families carry optional `subject_id` independently of derivation
`parent_id`. Contact requires a unique box or track; classification and presence
also accept a video-frame observation. Duplicates are ambiguous before geometry
validation. Native/browser contact consumers share these rules without content-type
filters or parent fallback. Plumber compares the corresponding subject source and
geometry before matching categorical identities, literal text or semantic decisions.
Reordering observations can match; swapping outcomes between subjects cannot.
Plumber retains producer component and instance identity in payload groups and
subject sources. Whole-frame subjects require exactly one addressable video
frame in each source group; ambiguous multi-frame groups fail comparison rather
than guessing correspondence from packet-local IDs, order, or image dimensions.
Empty collections report no observations; unavailable results do not establish matches.

## Gaze measurements

`GazeMeasurements.measurements` contains gaze observations, not generic poses.
Each observation has an optional `subject_id`, an optional `GazeAngles` pair
(`yaw_degrees`, `pitch_degrees`), and an optional confidence score. Absence is
explicit: an absent pair is unavailable, while a present `(0, 0)` is measured
straight-ahead gaze. Empty collections mean no observations. A missing score
does not invalidate otherwise valid angles; a supplied score must be finite
and in `[0,1]`, including valid zero.

Angles are finite degrees in the unmirrored camera/image convention. Image x
points right and y points down. The image-plane projection is
`dx = -sin(yaw) * cos(pitch)`, `dy = -sin(pitch)`: near forward gaze, positive
yaw projects left and positive pitch projects up. Consumers wrap angles modulo
360 before projection/comparison. The arrow is a 2D projection, not a full 3D
orientation display. Presentation owns arrow length and styling; measured zero
does not require a visible arrow.

`subject_id` refers to an observation's `ObjectMeta.id` in the same packet,
independently of the gaze object's derivation `parent_id`. It is not a persistent
track ID, a coordinate transform or an inferred nearest face. A present zero is
invalid. Native/browser renderers and Plumber resolve exactly one `BoxDetection`
or `ObjectTrack`, count duplicates before checking geometry, and require valid
explicit image space. Content-type strings do not select a gaze payload or
restrict its subject. The arrow starts at the projected subject rectangle's
center. Missing, duplicate and invalid subjects are not guessed or repaired.
Model input requirements remain unchanged: MobileGaze still needs a face crop.

`GazeDetectionParser` retains its soft-argmax mappings: configured positive bin
width, or the default range from -90 to +90 degrees. Non-finite logits in either
axis make the entire angle pair and confidence unavailable. For valid logits,
confidence is the minimum of the two maximum softmax-bin probabilities; it is
not a calibrated correctness probability. Malformed tensor shapes and invalid
bin-width configuration remain errors.

Plumber compares shortest per-axis differences modulo 360, taking the maximum
and dividing by 180. The existing `0.01` threshold is unchanged (1.8 degrees).
It also requires matching subject source and geometry, so reordering measurements can
match but swapping angles between subjects cannot. Unavailable/non-finite
measurements do not establish a match.

## Runtime Transport

Inside GStreamer, `FrameResultsMeta` attaches a shared `FrameResults` instance to
the corresponding `GstBuffer`. `opkinfer` creates the metadata before OpChain
execution; postprocessors add generated payloads through
`OpChainContext::frameResults`; `opktracker` and `opkperformance` can append more
payloads; and `opkosd` reads supported payload types for visualization.

At application boundaries, the generated wire envelope is serialized as bytes.
`opkcomm` publishes those bytes in the `frame_results_packet_b64` field with the
`perception-frame-results+base64` encoding marker. External consumers must use a
compatible released Open Perception Kit SDK to decode and access the typed payloads.
The embedded `opksink` WebUI uses the generated TypeScript SDK at this boundary;
it validates producer identity and converts supported typed payloads into its
established OSD and output-panel presentation model.

## Testing Python Guest Scripts

When the development build enables tests, it provides the non-installed
`python_guest_script_executor` binary for testing trusted Python transformations
against a live C++ `FrameResults` envelope. The executor accepts an ordered list
of scripts and calls `process(env)` from each script against the same envelope:

```bash
./development/build/tests/python_guest_script_executor \
  --output /tmp/results.bin \
  seed_boxes.py \
  custom_postprocessor.py
```

Scripts import the generated guest type for annotations and append results with
the generated Python object API:

```python
from open_perception_kit.guest import Envelope


def process(env: Envelope) -> None:
    ...
```

Existing payloads are read-only through the bridge. A transformation therefore
reads its input payloads and appends new payloads rather than mutating entries in
place. After all scripts return successfully, the executor writes a raw
FrameResults packet that tests can decode with `open_perception_kit.packet.decode`.
`--python-path` can be repeated to add script dependencies to the embedded
interpreter's module search path. The embedded runtime uses the same Python
installation selected by Meson, including that installation's virtualenv
packages such as the generated SDK's FlatBuffers dependency.

The executor runs CPython in-process and is not a security sandbox. It is test
tooling only and does not add Python postprocessors to production OpChains.

## Voice activity results

`VoiceActivityDetections` (`VACT`) contains `VoiceActivityDetection` items and the
standard `LayerInfo`/producer metadata. Its semantic content type is
`voiceActivity`. Each item describes one inference window, not an utterance:
Optional `speech_probability` is the model score and optional `speech_active` is the decision after
hysteresis and attack/release. `window_start_sample` and `window_end_sample`
identify a half-open interval of mono 16 kHz samples since the latest stream
reset. `discontinuity` marks the first window after a reset.
Absent probability/decision values mean unavailable, not zero probability or
inactive speech. Present zero and false remain valid. Shared consumers validate
finite probabilities, nonempty sample ranges and ordered-or-absent timestamp pairs.

Optional `window_start_ts_ns` and `window_end_ts_ns` describe the same interval
on the source media presentation timeline (PTS), with resampler delay removed.
They are not wall-clock times or inference completion times. Both are absent
when source PTS is unknown, the window includes negative filter preroll, or the
range cannot be represented. Timestamp zero is valid. Separate inference calls
append separate payloads, including when one source buffer produces several
windows. Consumers must iterate all payloads rather than reading only the first.
Audio window comparisons use producer identity and the explicit window/measurement
fields. An `ObjectMeta.parent_id` is derivation provenance and does not require
the source buffer's observation to remain in the current packet.

`SileroVadParser` reads the single speech probability from the `[1,1]` Float32
output; it does not apply softmax. `thresholdOn` (0.60) and
`thresholdOff` (0.45) provide hysteresis. `attackMs` and `releaseMs` (both 50)
are converted to exact sample thresholds using 16 samples/ms. Each consecutive
qualifying window contributes the effective audio hop. Decisions change only
at window boundaries; a 50 ms threshold with a 512-sample hop takes two calls
(64 ms). The parser rejects the obsolete `windowMs` attribute and resets its
state on `inferenceInfo.audio.discontinuity`. Each parser instance owns its counters.

SDK 1.0 keeps this root identity and adopts explicit measurement availability.
Producers and consumers use the same regenerated development snapshot. Native
inspection, browser decoding and Plumber all handle the typed audio roots.

## Turn detection results

`TurnDetections` (`TURN`) contains `TurnDetection` items with standard object,
layer and producer metadata. Its semantic content type is `turnDetection`.
`SmartTurnParser` appends one payload per model evaluation, including negative
assessments and retries. Optional `turn_probability` is the model's completion score in
`[0,1]`; optional `turn_complete` is true when that score meets the configured `threshold`
(default 0.5). It is a model hint, not a final conversation-controller decision.
The parser emits no diagnostic prints. Consumers iterate every payload rather
than interpreting repeated positives as separate completed utterances.

`window_start_sample` and exclusive `window_end_sample` describe the real mono
16 kHz context evaluated by the model, using the same sample timeline as VAD.
They exclude synthetic front padding and are not utterance boundaries. Optional
`window_start_ts_ns` and `window_end_ts_ns` carry the corresponding source media
PTS; both are absent if unavailable, and timestamp zero is valid. `discontinuity`
marks the first assessment after initialization, stream discontinuity or a gap
in the retained history. `TurnDetector` preserves this marker across skipped
VAD hops until the next model assessment. A history gap need not restart the
source sample counter; a stream reset does.
Turn gating requires the exact bound VAD producer, configured model/inference
element and current window. Duplicate or unusable matching results fail rather
than selecting a first/last producer or inventing silence. The conversation
example likewise rejects ambiguous producers/windows; valid confirmation and
silence-timeout rules are unchanged.

The conversation controller owns turn confirmation, deduplication, timeout
fallback and STT submission. Neither this payload nor the parser implements
those policies. Plumber normalizes and compares turn assessments using their
decision, audio bounds, timestamps and probability.

The optional score/decision migration is part of the breaking, unreleased SDK 1.0
contract. Root identities remain unchanged; generated bindings must be upgraded
together. Construction and serialization tests cover positive/negative results,
repeated assessments, empty payloads and optional/zero timestamps.
