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
draws before annotations; within each group, overlays draw in list order, with
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
| Classifications | `Classifications`: candidate text or PERSON/NON-PERSON status. |
| Gaze | `PoseEstimations` with `eyeYawPitch` semantics plus parent face geometry. |
| Camera contact | `Classifications` with `cameraContact` semantics plus parent face geometry. A dashed ring denotes no contact; a solid ring denotes contact. |
| Performance text | `PerformanceOverlay`: combined pipeline text, since the schema has no source provenance to separate producers. |
| Background replacement | Full-frame `SegmentationMasks` with `segmentation` semantics and `backgroundReplacement` compositing: graded background opacity over the original video. |

`FrameContext` supplies video geometry rather than a selectable visualization.

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
| `resolve(index, selection, settings)` | Resolve current-frame inputs and relationships. Return `state`, `message`, `items`, and feature-specific resolved data. |
| `render(target, resolved, settings)` | Draw resolved data. Target supplies canvas/context, video, mapper, shared layout state, time, and the owning overlay for resource caching. |

The editor offers registered working features even before discovery. The host
keeps unbound selections waiting; plugin resolvers receive selected sources.
The dispatcher knows only the plugin contract and overlay order. Feature-specific associations belong in the
resolver; reusable geometry/drawing belongs in `overlay-drawing.js`. A new plugin
module can be imported into the registry without changing the editor or dispatcher.

Semantic/instance/panoptic segmentation requires its own agreed class/instance and
coordinate contracts; those meanings cannot be inferred from generic bitmap bytes.

## Background replacement

Select **Background replacement**, then choose **Solid color** or **Local image**.
Images use a centered cover crop and retain their aspect ratio. The selected color
fills transparent image areas and is the fallback while an image loads, if it fails
to decode, or when no image is selected. **Clear image** removes the saved image.
PNG, JPEG and WebP inputs up to 10 MiB and 40 megapixels are accepted. They are
resized to a maximum side of 1920 pixels and encoded locally, capped at 2 MB of
saved data. Nothing is uploaded. Browser storage quotas can still prevent saving;
the normal save-failure notice then applies and the image works for this session.

The renderer requires one tightly packed `Uint8` mask for the full source frame,
without model-input letterboxing. A nonzero parent ID must resolve unambiguously
to the current `VideoFrame`; cropped-object masks are not stretched across the
frame. The mask is scaled into the contained video rectangle, leaving display
letterboxing untouched. Missing, ambiguous, invalid or oversized masks draw nothing.

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

In the browser, configure Tracked objects and toggle Boxes/Trails independently.
Select a tracker through Bounding boxes and verify no trails draw. Add a separate
Movement trails feature and verify its visibility does not change other overlays.
Exercise add/edit/cancel/remove, keyboard focus, one-to-many source discovery,
reload, disconnect/reconnect, missing parents, and unsupported metadata. Run a live
pipeline to inspect alignment: synthetic tests do not establish WebRTC timing.
Also add a feature before its source exists, reload, and then introduce one or
several compatible sources: only an unambiguous discovery may bind automatically.
