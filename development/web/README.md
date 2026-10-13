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
checkbox and an always-visible trash button for one-click removal. Click the row
title (or use Enter/Space) to expand its controls, status, and metadata below it.
Expanded rows stay open when metadata arrives or another overlay is added/removed.

The row background uses the selected overlay color, with contrasting title text.
Waiting and unsupported overlays use a light-grey row with grey text; an available
source with no detections retains its color. Visibility is independent of source
availability, so hiding a working overlay does not grey it out. Long titles stay
on one line with an ellipsis; hover for the full title and status.

Adjust controls immediately or choose **Change source…**. The editor applies
changes only when submitted; Cancel or Escape discards them. Overlays draw in
list order, with later entries on top. Adding overlapping features deliberately
draws each one; they never implicitly change each other's settings.

These controls affect browser drawing only. Decorations already encoded into
the video by server-side `opkosd` cannot be hidden by browser controls.

## Features and metadata

The UI selects a visualization, not a wire payload type. A feature can consume
multiple related payloads, and one payload can support multiple features.

`FrameContext` supplies video geometry rather than a selectable visualization.

Decoding requires the existing `perception-frame-results+base64` encoding and
exact SDK producer identity. Invalid/incompatible packets clear current drawing
and show a metadata error. Unknown payload families are not decoded by the current
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

Frames, resolved relationships, and bitmap data are never persisted. Settings
load/save failures show a notice and leave in-memory controls usable. Invalid
saved entries are skipped with a notice.

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

Rendering uses latest-arriving metadata, not frame-exact video matching. Geometry
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
| `controls` | Native checkbox, color, or range descriptors. |
| `defaults` | Default settings object, or function receiving the source descriptor (an empty object before a source is known). |
| `validateSettings(settings)` | Return normalized settings, or `null` for invalid saved data. |
| `discover(catalog)` | Return compatible primary descriptors from the observed-source Map. Match semantic requirements as well as family, and support empty payloads. Also used for unique, one-time binding of waiting overlays. |
| `resolve(index, selection, settings)` | Resolve current-frame inputs and relationships. Return `state`, `message`, `items`, and feature-specific resolved data. |
| `render(target, resolved, settings)` | Draw resolved data. Target supplies canvas/context, video, mapper, shared layout state, and time. |

The editor offers registered working features even before discovery. The host
keeps unbound selections waiting; plugin resolvers receive selected sources.
The dispatcher knows only the plugin contract and overlay order. Feature-specific associations belong in the
resolver; reusable geometry/drawing belongs in `overlay-drawing.js`. A new plugin
module can be imported into the registry without changing the editor or dispatcher.

Future mask/compositing plugins can read the original decoded `payload` through
their selected output's layers. Semantic/instance/panoptic segmentation and RVM
still require agreed class/instance, coordinate, and continuous-alpha contracts;
those meanings cannot be inferred from generic bitmap bytes.

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
