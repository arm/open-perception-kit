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

# WebUI rendering features

## Browser capture

The Mic and Cam controls request audio and video independently and share one
send-only WebRTC connection to `opksrc` on port 8003 (or `OPK_CONFIG.micWsPort`).
Both controls use the existing `pipeline_state.microphone` flag, which indicates
that `opksrc` is present. Turning off one device releases its tracks without
closing the other device; turning off both closes the connection. Permission
denial affects only the requested device. Control disconnect and page exit
release all capture devices.

Offers are serialized: adding or removing a device while an answer is pending
queues a new offer after that answer. The receiver must support repeated offers,
optional audio/video media sections, and explicit `capture` flags for stopped devices.
The native receiver supports Opus audio and VP8 video with independent fallback
to silence and black frames. Re-enabling a device reuses its session transceiver
with a replacement track. Stopping capture releases the hardware but retains a
send-only SDP section so older GStreamer receivers can resume the same pad.
Use localhost or an SSH tunnel; HTTPS support is outside this change.

## Visual design

The entire WebUI uses [Wafer](https://github.com/Arm-Debug/design-system) as its
visual source of truth: semantic colors and surfaces, Lato/Cousine typography,
spacing, control sizes, corner radii, focus, and interaction states. The system
light/dark preference is used automatically. An embedding can force a theme with
`data-theme="light"` or `data-theme="dark"` on `body`.

`content/style.css` is the single application stylesheet, not a skin over legacy
styles. UI icons are official Lucide SVG assets, rendered as CSS masks with
`currentColor`. Native buttons, selects, inputs, and the modal dialog retain
their keyboard behavior. Small visibility switches and right-side disclosure
arrows remain in the overlay list.

### Design assets

`npm run vendor` is the sole owner of the checked-in Wafer CSS/fonts and Lucide
SVG snapshot. It requires Node 22+, npm, Git, and GitHub CLI access to the design
system repository. It builds Wafer 2.0.2 at the pinned commit and verifies the
Lucide 1.61.0 package integrity before staging assets and original notices.
Normal builds and runtime are offline with respect to these assets; no CDN or
GitHub Packages credentials are needed by the browser, container build, or CI.

The only upstream CSS adjustment normalizes invalid multi-family `@font-face`
descriptors in Wafer 2.0.2 to single family names. Token values and typography
classes remain upstream-owned. The adjustment is recorded in `vendor/wafer/NOTICE`.

Product-specific exceptions are limited to video/canvas pixel geometry and user
visualization colors, resizable panel dimensions stored in pixels by
`layout-resize.js`, viewport breakpoints, one/two-pixel borders and focus outlines,
animation timing, native control rendering, and the OPK logo. Wafer has no tokens
for these contracts. They are owned here; revisit them when upstream supplies
corresponding patterns. Canvas annotation colors are data, not UI status colors.

## Overlay controls

Open **Overlays** (the layers button beside the video), then **Add overlay…**.
The panel stays open while you use model switches and other controls. Only the
Overlays button hides it; Escape still cancels an open add/edit dialog.
Choose a rendering feature in **Show**, then configure its controls. A single source is selected automatically
and shown as **From YOLO26n** (using the actual model or producer name). When
several sources match, a **From** dropdown lets you choose. The initial overlay
list is empty.

Supported features are available in **Show** even before their metadata arrives.
Configure an overlay's color and opacity while its model is disabled. It waits
until you enable the model and matching output
appears. A waiting overlay binds once to the single compatible source discovered
in this connection, using its real metadata identity. If multiple sources match,
it shows **Choose a source** instead of guessing; open its row and use **Change source…**.
OCR always requires an explicit source choice, even when only one compatible
output is available, because generic boxes/classifications do not identify an OCR task.
After binding, it never silently switches to another producer. Saved and unsaved
styles survive discovery, including while the editor is open.

Duplicate source names get only the smallest distinguishing metadata qualifier
needed. Full metadata is available in collapsed **Details** in the editor and
saved cards. Missing metadata is marked as not provided, never replaced with
guessed camera or producer names.

Each overlay is a single-line **feature · source** row with its own visibility
switch and an always-visible trash button for one-click removal. Click the row
title (or use Enter/Space) to expand its controls, status, and metadata below it.
Expanded rows stay open when metadata arrives or another overlay is added/removed.

The row uses a neutral Wafer surface and a small swatch of the selected overlay
color. Waiting and unsupported sources use a neutral swatch and secondary title
text; their status is also written out. An available source with no detections
retains its swatch color. Visibility is independent of source availability. Long
titles stay on one line with an ellipsis; hover for the full title and status.

In saved overlay rows, range and color controls preview immediately and save when
the adjustment is committed (`change`), rather than rewriting saved images on each
`input` event. Other row actions save immediately. Choose **Change source…** to edit the source.
Each editor starts from that overlay's saved selection, so cancelled choices do
not carry into another overlay. The editor applies
changes only when submitted; Cancel or Escape discards them. Background replacement
and semantic class maps draw before annotations; within each group, overlays draw in list order, with
later entries on top. Adding overlapping features deliberately draws each one;
they never implicitly change each other's settings.

These controls affect browser drawing only. Decorations already encoded into
the video by server-side `opkosd` cannot be hidden by browser controls.

## Features and metadata

The UI selects a visualization, not a wire payload type. A feature can consume
multiple related payloads, and one payload can support multiple features.

| Feature | Inputs and presentation |
| --- | --- |
| Bounding boxes | Non-face `BoxDetections` or `ObjectTracks`: rectangular boxes and labels. |
| Face circles | `BoxDetections` or `ObjectTracks` with `humanFace` semantics: circles around faces. |
| Tracked objects | `ObjectTracks` plus optional related `TrackTraces`: independently controlled **Boxes** and **Trails**, both enabled by default. |
| Movement trails | `TrackTraces`: trajectories without boxes. |
| Classifications | `Classifications`: explicitly declared categorical or recognized-text results; `PersonPresences`: explicit PERSON/NON-PERSON decisions with optional raw-score diagnostics. |
| Gaze | `GazeMeasurements` with finite degree-angle pairs and an explicit subject observation ID. Exactly one spatially valid `BoxDetection` or `ObjectTrack` supplies the arrow's center. |
| Camera contact | `CameraContacts` plus the uniquely referenced box/track subject. Explicit outcomes determine style: dashed for no contact, solid for contact. Unavailable results have no marker. |
| Performance text | `PerformanceMeasurements`: numeric timings and FPS formatted as combined pipeline text; producer and scope IDs keep metric histories independent of labels and order. |
| Background replacement | `SegmentationMasks` declaring `BackgroundOpacity` and an explicit image region: graded background opacity over the original video. |
| Semantic segmentation | `SemanticSegmentationMasks` with declared label namespace, encoding, image region and optional void value; per-class controls and current-frame presence. |
| Instance segmentation | `InstanceSegmentationMasks` with binary membership and declared image regions; contrasting outlines and optional boxes/class/confidence labels. |
| OCR | Explicitly selected recognized-text `Classifications` with `BoxDetections` subjects, or a `BoxDetections` source directly; explicit geometry and independent text/region controls. |

`FrameContext` supplies video geometry rather than a selectable visualization.

Classification candidate order is explicit in the list; coordinate fields no
longer encode rank. Label-set identity is retained separately from display text.
Existing contact/presence overlay selections are migrated to the new payload
names without resetting style or visibility. This does not decode old packets.

Gaze subjects use `subject_id`, independently of derivation parents and persistent
track IDs. Duplicate subjects are ambiguous even if one has invalid geometry.
Missing/invalid subjects and unavailable/non-finite angles produce no arrow;
measured zero remains distinct from unavailable. The projection uses unmirrored
image axes (positive yaw left, positive pitch up near forward gaze). Gaze selection
and subject eligibility do not depend on content-type strings. Saved gaze overlay
selections migrate to the new root while preserving their settings.

Box and trace geometry carries its own source-image basis. The decoder resolves
crop-local/normalized coordinates before rendering; neither derivation parents
nor the first frame record select a box's coordinate space. Unplaceable geometry
is omitted from overlays and reported as unavailable in the output panel.

Decoding requires the existing `perception-frame-results+base64` encoding and
exact SDK producer identity. Invalid/incompatible matched packets clear current
drawing; live-channel decode failures also show a metadata error. Unknown payload families are not decoded by the current
typed adapter. Only registered rendering features appear in **Show**; decoding
a payload does not imply that a renderer is registered for it.

## Source identity and lifecycle

Primary selections match payload family, semantic content, engine, model,
inference element, label family, and producer instance/component/implementation.
They do not depend on payload order, detection count, or per-frame object IDs.
Repeated payloads with identical provenance (for example, per-face crop iterations)
form one selectable source. Missing identity fields are not inferred.

Preferences are stored per browser origin under `opk-overlays:v2`:

```json
{
  "featureId": "tracked-objects",
  "selection": {"primary": {"...": "existing full source selector"}},
  "enabled": true,
  "settings": {"boxes": true, "trails": true, "color": "#ff3030", "opacity": 1}
}
```

Frames, resolved relationships, and inference mask bytes are never persisted.
Selected background images are saved locally as bounded raster data in the overlay
settings. Settings load/save failures show a notice and leave in-memory controls
usable. Invalid saved entries are skipped with a notice.

Before a source is known, `selection.primary` is `null`. That waiting selection
survives reloads. Its full source descriptor is saved when uniquely discovered;
no producer names or identifiers are invented in advance. Features whose renderer
is unsupported still cannot be added as working overlays.

- **Ready**: the feature's inputs are available in the latest metadata frame.
- **Ready · no results**: the selected payload arrived but contains no results.
- **Partial**: related data is missing. Tracked-object boxes can still draw while
  waiting for trails; parent-linked markers without parents are omitted.
- **Ambiguous**: several related candidates match. Only unambiguous items draw;
  the resolver never picks an arbitrary first candidate.
- **Unsupported**: the feature or semantic interpretation is unsupported.
- **Hidden**: the source is present, but the user unchecked the overlay.
- **Waiting for source**: configuration remains, but no matching primary payload
  exists in the current frame. Previous-frame detections are not drawn.
- **Choose a source**: an unbound overlay has multiple compatible observed sources
  and needs an explicit choice. Once selected, normal source matching applies.

Discovery remembers source descriptors until metadata disconnects; empty results
do not remove options. Disconnect clears live frames and discovery, while saved
overlays remain. Reloads and pipeline restarts resume a feature only when the same
primary identity returns. There is no pipeline UUID in this contract: a different
pipeline reusing all the same identity fields is indistinguishable.

Saved and draft sources remain selectable while editing, even if unavailable.
Discovering more sources reveals **From** without changing the selection or draft
styles. Presentation labels never change the full source key.

### Migration from payload-based overlays

Previously saved **Bounding boxes** overlays bound to `humanFace` sources are
automatically relabelled **Face circles**, preserving their source, color, opacity,
visibility, and order. Unbound bounding-box overlays remain unchanged.

The first load without a v2 record migrates `opk-overlays:v1`, retaining primary
selectors, order, visibility, color, and opacity. The old record remains as a
backup; unsuccessful writes leave migrated overlays usable in memory. Unknown
feature records remain visible as unsupported rather than being dropped.

Old tracked-box overlays become **Tracked objects** with **Boxes** enabled. If a
separate trail overlay previously overrode that source, migration initializes
the tracked feature's **Trails** checkbox to false, preserving the prior drawing.
Otherwise it starts true. This is a one-time conversion: afterward each feature
owns its controls, with no cross-overlay override behavior.

## Relationships and geometry

`indexFrame()` builds output, payload-family, producer, object-ID, and
producer/track-ID indexes once per incoming frame. Entries retain all candidates,
including ambiguous identities. Discovery caches only descriptors; indexes are
replaced on the next message and cleared on disconnect.

- Tracked-object trails join by producer component/instance and `trackId`.
  Model names, engine labels, and label families are not relationship keys.
  Missing producer identity or trail history gives a partial result. Multiple
  matching trails are ambiguous and omitted. No browser-side history is invented.
- Gaze and camera contact join `parentId` to face object IDs in the full frame,
  independently of whether face geometry is visibly drawn. Missing or ambiguous
  parents are never guessed.
- The persistent full selector still determines which primary output the user
  chose. It is intentionally separate from semantic relationship keys.

Rendering uses WebRTC metadata matched to the displayed frame's RTP timestamp.
Missing, expired or invalid matched metadata clears the overlay; there is no
fallback to the independently arriving metadata stream. Browsers without
`requestVideoFrameCallback` do not render overlays. The independent metadata
channel still supplies discovery and inspection before video is connected.
UI and image-loading redraws reuse only the displayed frame's matched results. Geometry
prefers `FrameContext` video dimensions, otherwise received video dimensions. The
mapper scales source-image coordinates into the contained video rectangle; it
does not apply a new crop/letterbox transform. Producers must supply coordinates
in that source-image space.

## Plugin contract

Plugins are ordinary checked-in modules under `src/features/`, registered in
`OVERLAY_FEATURES` in `src/overlay-features.js`. They do not modify metadata or
load external code. `defineFeature` in `src/overlay-feature.js` shares source
lookup, the default palette, and control validation. Each module owns its matching,
semantic resolution, rendering, and optional default overrides.

| Member | Contract |
| --- | --- |
| `id`, `label` | Stable saved feature ID and friendly Show label. |
| `controls` | Native checkbox, color, range, select, or local image descriptors. |
| `renderOrder` | Optional numeric drawing priority (default `0`); backgrounds use `-1`. Stable list order is preserved within a priority. |
| `defaults` | Default settings object, or function receiving the source descriptor (an empty object before a source is known). |
| `validateSettings(settings)` | Return normalized settings, or `null` for invalid saved data. |
| `discover(catalog)` | Return compatible primary descriptors from the observed-source Map. Match semantic requirements as well as family, and support empty payloads. Also used for unique, one-time binding of waiting overlays. |
| `requiresExplicitSource` | Optional flag: always offer a source picker when candidates exist and never auto-bind a pending overlay. Used by OCR, whose payload types are shared with other tasks. |
| `resolve(index, selection, settings)` | Resolve current-frame inputs and relationships. Return `state`, `message`, `items`, and feature-specific resolved data. |
| `render(target, resolved, settings)` | Draw resolved data. Target supplies canvas/context, video, mapper, shared layout state, time, and the owning overlay for resource caching. |
| `renderAnnotations(target, resolved, settings)` | Optional annotation pass for raster features, drawn at priority `0` after backgrounds. The resolved inputs and opacity are shared with `render`; stable overlay order applies within each priority. |
| `legend(resolved, settings)` | Optional `{entries: [{id, label, color, present}], message}` for the expanded row. Uses displayed-frame results; controls retain their DOM nodes across frames. |
| `classControls` | Optional native control descriptors for legend entries that also supply `enabled` and `opacity`. The feature validates per-ID overrides in `settings.classes`; presence and mask data are never saved. |

The editor offers registered working features even before discovery. The host
keeps unbound selections waiting; plugin resolvers receive selected sources.
The dispatcher knows only the plugin contract and overlay order. Feature-specific associations belong in the
resolver; reusable geometry/drawing belongs in `overlay-drawing.js`. A new plugin
module can be imported into the registry without changing the editor or dispatcher.

Panoptic segmentation requires its own agreed rendering contract; that meaning
cannot be inferred from generic bitmap bytes.

## Semantic segmentation

Choose **Semantic segmentation** and select its source, then adjust **Opacity**
(initially 50%). Expand the row for the class list. Classes are discovered from
the selected source's mask IDs, or restored from saved per-class preferences.
Each compact row contains a visibility switch, numeric label such as **Class 7**,
color picker, and opacity slider.
Click a class name to enter a local display name (up to 80 characters), then press
Enter or move focus away to save it. Clear the field to restore **Class 7**-style
numeric labels. Names are saved per overlay and survive reloads; they never
change the class IDs or incoming metadata. Spaces around a name are trimmed.
**Reset labels** restores all names in that overlay to their numeric defaults,
preserving colors, opacity, and visibility; the reset is saved immediately.
Hover the name for its ID and **In frame** or **Not in frame** status. Presence
follows matched metadata even when the overlay or class is hidden. **Unavailable**
means there is no valid matched output, rather than asserting the class is absent. Controls
retain class-specific accessible labels without visible field labels.
Rows are ordered by ID. Observed rows stay available when temporarily absent,
until the list is rebuilt; no complete model class inventory is assumed.

Class opacity multiplies the master overlay opacity: 50% class opacity and 50%
overlay opacity produce 25% effective opacity. Switching a class off makes its
pixels transparent, revealing the original video (or overlays underneath).
Class settings can be edited even while the model or overlay is disabled.
Color/range adjustments preview immediately and save on `change`; switches save
immediately. Incoming frames update presence without rebuilding the controls or
interrupting keyboard focus. Preferences are independent for each overlay.

The renderer has no dataset-specific class names or palettes. Metadata contains
class IDs and an explicit label-set namespace, but no class-name dictionary or colors.
Default colors are generated deterministically from the numeric IDs; they convey
neither class meaning nor confidence. `labelFamily` remains part of source
identity but does not select a browser-side lookup table. Class names in ONNX
model metadata are not currently carried in FrameResults. Previously saved
per-class colors, visibility, and opacity still apply to their IDs.

Only the payload's optional `voidValue` is transparent and excluded from observed
classes. Neither `0` nor `255` is implicitly void. This is a class map, not an opacity mask.

Rendering requires one tightly packed `Uint8` or little-endian `Uint32LE` class map
per selected source and a valid `ImageSpace`. The grid spans the declared region;
its resolution need not equal the source dimensions. No derivation parent selects
placement. Grid dimensions are bounded to 8192 per side and 16,777,216 pixels,
with at most 4096 distinct non-void classes/preferences. Invalid data and duplicate
maps draw nothing and show a status. Nearest-neighbor placement respects crop
origins and extents without reapplying model-input letterboxing.

The mask and presence indicators use only metadata matched to the displayed video
frame. Unmatched frames clear the mask; explicit empty payloads are successful
empty results. Independent live
metadata only discovers sources. Colorized pixels are cached per overlay and
invalidated when its mask or class styles change. Source, visibility, order,
master opacity, and per-class names/visibility/color/opacity are saved; class maps and
presence are transient. Existing opacity-only preferences load with default
class styles. Instance masks and background-replacement masks remain separate
features and cannot bind to a semantic overlay.

## Instance segmentation

Choose **Instance segmentation** to show separate objects from an
`InstanceSegmentationMasks` source. It reuses the semantic feature's compact
class switches, colors, opacity, editable names, and **Reset labels**. Preferences
remain independent per overlay and source. No dataset-specific labels are built
in. Class IDs are optional unsigned values through `4294967295`. Absence displays
as **Unclassified**, not class zero. The saved UI key `-1` retains unclassified
style preferences; it is not written back as a metadata class ID.

Each instance requires a `Uint8` bitmap containing only `0` and `1`:
`0` is outside the object, and `1` is foreground at the configured class opacity.
It is neither a per-pixel class ID nor an alpha byte. Empty all-zero masks do not
draw or mark their class present. Each nonempty mask has a contrasting one-grid-
pixel outline, so touching objects of the same class are distinguishable.
**Boxes and labels** (enabled by default) adds each object's white bounding box, class
name, available confidence, and frame-local number (`#1`, `#2`, ...), when a box is
provided. Missing boxes do not suppress masks. Numbers follow payload
order and may change every frame; they are not tracking identities.

The class's selected color fills its masks. Black or white outlines are chosen
for contrast with that color. Later visible instances in payload order overwrite
earlier ones where their foreground pixels overlap; alpha does not accumulate
per overlapping mask. Hiding a class reveals any visible instance underneath.
The final raster is blended using the master opacity. Class opacity also applies
to its boxes and labels, and label changes do not invalidate cached mask pixels.

All masks for a source must declare the same source-frame identity/dimensions and
compatible class namespaces. Their grids and crop regions may differ. Bitmap
`ImageSpace` is authoritative for placement; optional boxes are annotations with
their own declared units/space, not mask placement transforms. Boxes are projected
and clipped for drawing. Optional confidence must be finite and within `[0,1]`.
The renderer rejects the selected source's
entire mask set when an item is malformed, rather than displaying a misleading
partial result. It supports at most 512 masks and 67,108,864 total mask pixels per
source/frame, checked before scanning pixel values. This aggregate budget is
separate from the unchanged per-bitmap limits (8192 per side, 16,777,216 pixels)
and applies the same per-image bound to the display-pixel composition surface. Binary validation and
compositing remain bounded even when many large masks are supplied.
The entire bitmap is validated; actual foreground bounds then limit compositing
work, independently of the supplied annotation box.

Drawing uses only displayed-frame-matched metadata and nearest-neighbor scaling
inside the contained video rectangle. Missing/unmatched frames clear both raster
and annotations. Mask validation is cached per indexed output, and one composite
canvas and reusable scratch raster are cached per overlay. Native canvas
destination-out/source-over composition makes later membership replace earlier
pixels without accumulating opacity, including for differently sized crop grids.
Composition uses the displayed video dimensions and device-pixel ratio rather
than source-image resolution, so fine crop grids are not prematurely downsampled.
Resize and DPR changes invalidate the raster cache even on a paused frame.
Displays exceeding 8192 pixels per side or 16,777,216 composition pixels use a
proportionally reduced intermediate surface; pixel-exact rendering above that
budget would require tiled composition. Source-frame dimensions describe a
coordinate basis and do not determine the intermediate allocation.

## OCR

Choose **OCR**, then explicitly select **From**:

- A `Classifications` source supplies `RecognizedText` records with no categorical
  namespace/ID. Each `subject_id` must uniquely identify a current-frame
  `BoxDetections` object. Geometry comes from that subject's declared image space,
  not derivation parents, candidate coordinates or array order.
  Only the selected recognition source contributes text.
- A `BoxDetections` source supplies regions directly. Its optional `text` field
  is displayed when populated; otherwise the view is region-only. This mode
  does not borrow results from another classification source.

The OCR feature routes on these schema payload types and object relationships,
not `contentType`, model names, or parser names. Shared source identity still
retains the existing provenance fields; this is not a repository-wide removal
of the legacy content-type contract. Generic box/classification producers can
also be selected intentionally. When a pending overlay discovers outputs, use
**Change source…** to choose one rather than automatically binding to the first.

**Regions** and **Recognized text** are independently enabled by default.
Nonempty text gets a solid region outline and a white-on-dark label above the
region, or below it when necessary. Empty text uses a dashed outline without
inventing a recognition result. Labels and outlines stay inside the contained
video rectangle; regions are not filled. **Color** affects outlines, **Opacity**
affects the overlay, and **Text size** selects a fixed screen-pixel font size.
OCR text is bounded to 512 Unicode code points before layout, and labels are
ellipsized to the available width. Whitespace is collapsed for the single-line overlay;
the full original text remains available in the inference panel and copy output.
Text is drawn as data, never evaluated as HTML.
Fitted labels are reused between redraws of the same matched frame. A new frame,
display width, font size, or text/score selection refits them; each overlay retains
only its current layout.

**Region confidence** and **Text confidence** are independent and off by default.
The region score comes from the box; the text score comes from its linked
recognition candidate. Enabling both shows, for example,
`ABC 1234 · Region: 80.0% · Text: 96.3%`. Either score can be shown without the
recognized string or region outline. Zero scores are retained even for empty
recognized text; absent/ambiguous text scores never fall back to a region score.
A detector-only source has only the region score. Long recognized strings are
shortened first to leave room for requested scores. Existing saved **Confidence**
settings enable both controls, unless a new explicit choice is already saved.
In EXPKITS-1443, the text score is the mean retained-character probability after
CTC decoding, not a calibrated probability that the entire string is correct.

The supported geometry is a finite, positive axis-aligned box with explicit image
space and units. Crop-local and normalized boxes are projected before rendering;
no `FrameContext` parent lookup or dimension heuristic is used. The OCR parser exports
axis-aligned envelopes, not its internal rotated quads; rotated/perspective
regions are not inferred from axis-aligned boxes. Missing or duplicate subject
objects omit the affected regions. Multiple recognition records/candidates for
one region retain an unlabelled outline and report ambiguity rather than choosing
arbitrary text. Malformed geometry/text/scores are omitted with status feedback;
outputs exceeding 4096 records are rejected.

Only displayed-frame-matched metadata draws. Orphan recognition results never
become floating labels, and absent/unmatched frames clear the overlay. Preferences
persist per selected source/overlay; recognized strings and frame relationships
are not saved. For detector regions that remain visible while recognition is
disabled, add a separate detector-bound OCR overlay and disable **Regions** on
the recognition overlay to avoid duplicate outlines.
See [mask representation](../../docs/arch/perception.md#mask-representation) for
the shared SDK contract used by the overlay validators.

## Background replacement

Select **Background replacement**, then choose **Solid color** or **Local image**.
Images use a centered cover crop and retain their aspect ratio. The selected color
fills transparent image areas and is the fallback while an image loads, if it fails
to decode, or when no image is selected. **Clear image** removes the saved image.
PNG, JPEG and WebP inputs up to 10 MiB and 40 megapixels are accepted. They are
resized to a maximum side of 1920 pixels and encoded locally, capped at 2 MB of
saved data. Nothing is uploaded. Browser storage quotas can still prevent saving;
the normal save-failure notice then applies and the image works for this session.

The renderer requires one tightly packed `Uint8` mask explicitly declaring
`BackgroundOpacity`, without model-input letterboxing. Its `ImageSpace` determines
placement independently of parent IDs and compositing strings. Cropped masks
cover only their declared region within the contained video rectangle, leaving
the surrounding video and display letterboxing untouched. Missing, ambiguous,
invalid or oversized masks draw nothing. The existing browser resource limit is
8192 per dimension and 16,777,216 samples; it is not a wire-format limit.

Mask bytes are **background opacity**: `0` keeps the video, `255` replaces it,
and intermediate values blend continuously. `RvmParser` converts foreground alpha
with `round(255 * (1 - clamp(alpha, 0, 1)))` and rejects non-finite values. The
browser applies no binary threshold; the Opacity control multiplies this alpha.
The existing native `opkosd` replacement path still uses its binary mask threshold
of 150. Omit that native replacement when testing browser backgrounds.

The compositor uses the original video colors, not RVM's separate inferred
foreground-color tensor. As with the other overlays, its mask comes from metadata
matched to the displayed video frame. Hiding/removing the overlay,
missing metadata and disconnects reveal the original video. Decoded images and
scratch canvases are cached per overlay and become collectible when it is removed;
replacing/clearing an image drops its old decoded resource.

## Module responsibilities

- `src/frame-results.js`: generated SDK decoding, payload family/provenance, and
  item adaptation. Both detector and tracker outputs remain available.
- `src/overlay-sources.js`: source identities, concise labels, current-frame indexes.
- `src/overlay-features.js`: registry, lookup, resolution dispatch, and status text.
- `src/overlay-feature.js`: shared feature contract, palette, and parent lookup.
- `src/features/`: individual rendering features.
- `src/features/segmentation.js`: shared full-frame mask validation and generic class preferences, colors, and legends for semantic/instance features.
- `src/overlay-drawing.js`: coordinate mapping and drawing primitives.
- `src/osd-renderer.js`: ordered dispatch and isolated canvas state; overlay opacity
  multiplies the renderer's alpha.
- `src/overlays.js`: validated feature records, persistence, one-time source
  binding, and v1 migration.
- `src/client-osd.js`: discovery, native dialog, overlay list, and control binding.

## Validation

From the repository root, in the OPK development container:

```sh
./scripts/opksink-web.sh test
./scripts/opksink-web.sh generate
./scripts/opksink-web.sh check
```

Tests cover discovery, relationship ambiguity, independent Boxes/Trails controls,
source/style isolation, hidden-parent geometry, migration/validation, concise
labels, editing during discovery, plugin registration, decoder behavior, and
disconnect notifications.
Foundation tests use non-drawing fixture plugins. Tests under `tests/features/`
exercise individual registered features, so feature commits remain independently
testable.

The semantic feature tests cover zero/unknown IDs, palette stability, malformed
maps, parent geometry, source isolation, and cached colorization. The controlled
Chromium test in `.github/scripts/quick-start-smoke/smoke.spec.js` checks actual
canvas pixels at DPR 2, nearest-neighbor boundaries, display letterboxing,
matched-frame clearing, per-class alpha/color, uninterrupted control focus, and
saved opacity/visibility/class preferences. Run it through
that directory's Playwright configuration with `--grep 'semantic class maps'`.
Instance coverage checks binary foreground, overlapping and same-class masks,
outlines, frame-local labels, signed/wide class IDs, aggregate limits, independent
class preferences, and clearing. Run its browser test with `--grep 'instance masks'`.
OCR coverage checks explicit selection, parent-ID joins, missing/ambiguous data,
Unicode and literal markup, confidence semantics, label placement/clipping,
displayed-frame clearing, and persisted controls. Run its browser test with
`--grep 'OCR overlays'`. Wire fixtures follow the BoxDetections/Classifications
contract produced by the EXPKITS-1443 OCR branch; a live OCR pipeline requires
that runtime and matching producer/browser SDK identities.

In the browser, configure Tracked objects and toggle Boxes/Trails independently.
Select a tracker through Bounding boxes and verify no trails draw. Add a separate
Movement trails feature and verify its visibility does not change other overlays.
Exercise add/edit/cancel/remove, keyboard focus, one-to-many source discovery,
reload, disconnect/reconnect, missing parents, and unsupported metadata. Run a live
pipeline to inspect alignment: synthetic tests do not establish WebRTC timing.
Also add a feature before its source exists, reload, and then introduce one or
several compatible sources: only an unambiguous discovery may bind automatically.
