# Metadata AGENTS Guide

This file is the agent-oriented integration contract for building applications on top of AMP metadata.

Use it when you are building any application that consumes the JSON metadata stream exposed from AMP.

The goal is simple:
- connect to the metadata stream
- parse the JSON reliably
- understand the current field semantics
- avoid making unstated assumptions about contracts that are not stable yet

## Scope

This guide covers the current external JSON metadata path in this repository:
- source runtime model: `development/common/pek/Perception.h`
- serializer: `development/common/pek/PerceptionSerializer.cpp`
- transport element: `development/elements/pekcomm`
- current reusable Python client: `metadata/api/json_stream.py`
- current example app: `apps/tcp_dump/dump_stream.py`
- current transport schema: `metadata/api/perception-message.schema.json`
- current payload schema: `metadata/api/perception.schema.json`

This guide does not describe:
- browser UI behavior in `ampsink`
- WebRTC media delivery
- a future schema-first public API

## Current Contract Level

Treat the current JSON as a **repo-local integration contract**, not a long-term product-stable public API.

Important consequences:
- field names are defined by handwritten serializer code
- the JSON matches the serializer, not necessarily `Perception.h` member names exactly
- new metadata types may require C++ changes in `Perception.h`, `PerceptionSerializer.h`, and `PerceptionSerializer.cpp`

If you are building an app, prefer:
- validating transport messages against `metadata/api/perception-message.schema.json`
- validating non-null payloads against `metadata/api/perception.schema.json`
- handling unknown `contentType` values gracefully
- handling unknown detection `type` values gracefully

## Transport

### Current transport options

Use an `pekcomm` JSON stream exposed by:
- TCP
- WebSocket

The payload is the same in both cases:
- one transport message object per published message
- the transport message contains `frame_counter` and `perception`
- no explicit version field in the message itself

Current pipeline example:
- `config/pipelines/gaze-detection.json`

Current TCP insertion example:
- `pekcomm method=tcp tcp-host=0.0.0.0 tcp-port=7001 !`

Current WebSocket insertion example:
- `pekcomm method=websocket ws-port=8002 endpoint=/ws !`

### TCP transport

Current TCP behavior:
- `pekcomm` runs as a TCP **server**
- it listens on `tcp-host:tcp-port`
- client applications connect to it
- each message is newline-delimited JSON

This is visible in the current TCP writer implementation:
- `development/elements/pekcomm/tcp_writer.cpp`

Current default port:
- `7001`

Container publishing:
- `compose.base.yaml` publishes `7001:7001`

### WebSocket transport

Current WebSocket behavior:
- `pekcomm` runs as a WebSocket **server**
- it listens on `ws-port` and serves the configured `endpoint`
- browser or non-browser clients connect to it
- each published metadata message is sent as one WebSocket text frame

Current default port:
- `8002`

Recommended browser use:
- use the WebSocket transport for browser-hosted tools, games, dashboards, and inspection UIs

Recommended non-browser use:
- TCP is still simpler for small local tools and CLI consumers

### Wire format and framing

TCP stream:
- UTF-8 JSON text
- one JSON object per line
- newline-delimited

WebSocket stream:
- UTF-8 JSON text
- one JSON object per text frame
- no newline framing required by the client, even if internal server code preserves the same serialized payload shape

There is currently:
- no explicit schema/version field in each message
- no sequence number beyond `frame_counter`

Each line is a transport message shaped like:

```json
{
  "frame_counter": 0,
  "perception": {
    "perfdata": [],
    "layers": []
  }
}
```

The inner `perception` object is the serialized top-level `Perception` payload.

## Client Patterns

Reusable thin client:
- `metadata/api/json_stream.py`

That helper currently covers:
- TCP newline-delimited JSON

Example TCP usage:

```python
from metadata.api import JsonStreamClient

with JsonStreamClient(host="127.0.0.1", port=7001) as client:
    for message in client.iter_messages():
        perception = message.get("perception")
        if perception is None:
            continue
        print(perception["layers"])
```

Terminal dumper:
- `apps/tcp_dump/dump_stream.py`

TCP dumper example:

```bash
python3 apps/tcp_dump/dump_stream.py --host 127.0.0.1 --port 7001
```

Or:

```bash
./apps/tcp_dump/dump_stream.py --host 127.0.0.1 --port 7001
```

### Browser WebSocket client

For browser apps, connect directly with the standard WebSocket API:

```js
const ws = new WebSocket("ws://127.0.0.1:8002/ws");

ws.onmessage = (event) => {
  const message = JSON.parse(event.data);
  const perception = message.perception;
  if (!perception) return;
  console.log(perception.layers);
};
```

### Browser app recommendation

For browser-based tools or games:
- use `ampsink` WebRTC for video/audio if needed
- use `pekcomm` WebSocket for metadata
- do not try to infer metadata from the video stream itself

## Transport Message Shape

The top-level JSON transport message currently looks like:

```json
{
  "frame_counter": 0,
  "perception": {
    "perfdata": [],
    "layers": []
  }
}
```

This transport shape is defined by:
- `development/elements/pekcomm/writer.cpp`
- `metadata/api/perception-message.schema.json`

### `frame_counter`

Type:
- integer

Meaning:
- transport-level running frame counter from `pekcomm`

Do not use this as a stable globally unique identifier.

### `perception`

Type:
- `Perception` object or `null`

Meaning:
- the serialized runtime metadata payload for the current message

Applications should:
- handle `null` gracefully
- treat `perception.schema.json` as the schema for the non-null payload

## Inner `Perception` Payload Shape

The inner `perception` object currently looks like:

```json
{
  "perfdata": [],
  "layers": []
}
```

This inner payload is defined by:
- `development/common/pek/PerceptionSerializer.cpp`
- `metadata/api/perception.schema.json`

### `perfdata`

Type:
- array of strings

Meaning:
- human-readable performance lines
- mainly for debugging/overlay/UI

Do not treat these strings as a stable machine contract.

### `layers`

Type:
- array of layer objects

Each layer currently contains:
- `engine`
- `model`
- `tags`
- `labelFamily`
- `contentType`
- `compositingMode`
- `detections`
- `infer-id`

Important:
- the serializer emits `"infer-id"`
- not `inferElementId`

This is a current serializer quirk and is intentional in the schema.

## Layer Semantics

Think of each layer as:
- one inference or processing step
- with one semantic result category
- containing zero or more detections/results

### Key fields

`engine`
- runtime backend identifier
- examples: ONNX runtime, tracker, etc.

`model`
- model identifier/name

`tags`
- free-form string used by the runtime/config

`labelFamily`
- label namespace if applicable
- examples include things like `coco`

`contentType`
- the main semantic routing field
- downstream code often keys off this field

`compositingMode`
- rendering hint
- relevant mostly for overlay behavior

`infer-id`
- source element identifier from runtime

## Detection Encoding

Each detection is encoded as:

```json
{
  "type": "YawPitch",
  "data": { ... }
}
```

The current known detection types are:
- `Rect`
- `YawPitch`
- `LocalizedText`
- `SegmentationMap`
- `TrackTrace`
- `ObjectEmbedding`
- `VideoFrame`
- `Classification`
- `AudioFrame`
- `PersonClassification`

Applications should:
- dispatch primarily on `type`
- use `contentType` to interpret the semantic role of the detection

## Common Object Fields

Most detection payloads inherit common object fields:
- `uuid`
- `parentUuid`
- `creationTsNs`

### Meaning

`uuid`
- unique identifier for this detection/object instance

`parentUuid`
- link to a logically related parent object
- often used to attach derived results to a face or frame

`creationTsNs`
- timestamp in nanoseconds from runtime creation

Applications should use:
- `parentUuid` for relationship joins
- not array position

## Frequently Used Content Types

These are the most useful current `contentType` values for applications:

### `humanFace`

Usually contains:
- `Rect`

Meaning:
- face detections

Useful for:
- anchoring face-local UI
- joining gaze/contact results through `parentUuid`

### `genericObject`

Usually contains:
- `Rect`

Meaning:
- generic object boxes

### `eyeYawPitch`

Usually contains:
- `YawPitch`

Meaning:
- gaze direction estimate associated with a face

### `cameraContact`

Usually contains:
- `Classification`

Meaning:
- whether the subject is looking at the camera

### `classification`

Usually contains:
- `Classification`

Meaning:
- top-k classification candidates

### `personClassification`

Usually contains:
- `PersonClassification`

Meaning:
- person / non-person yes-no style output

### `trackTrace`

Usually contains:
- `TrackTrace`

Meaning:
- temporal tracker history

### `segmentation`

Usually contains:
- `SegmentationMap`

Meaning:
- dense pixel-level segmentation output

## Detection Payload Shapes

### `Rect`

Fields:
- `uuid`
- `parentUuid`
- `creationTsNs`
- `x`
- `y`
- `width`
- `height`
- `confidence`
- `classId`
- optional `text`

Notes:
- `text` may be omitted when empty
- coordinates are interpreted by OSD as image-space coordinates

### `YawPitch`

Fields:
- `uuid`
- `parentUuid`
- `creationTsNs`
- `confidence`
- `yaw`
- `pitch`

Units:
- **degrees**

Important:
- JSON stores degrees, not radians
- OSD converts to radians only for drawing

### `Classification`

Fields:
- `uuid`
- `parentUuid`
- `creationTsNs`
- `candidates`

Each candidate has:
- `confidence`
- `classId`
- `text`
- `x`
- `y`
- `w`
- `h`

### `LocalizedText`

Fields:
- `uuid`
- `parentUuid`
- `creationTsNs`
- `x`
- `y`
- `w`
- `h`
- `text`

### `VideoFrame`

Fields:
- `uuid`
- `parentUuid`
- `creationTsNs`
- `originalWidth`
- `originalHeight`
- `sourceCropLeft`
- `sourceCropRight`
- `sourceCropTop`
- `sourceCropBottom`
- `letterboxLeft`
- `letterboxRight`
- `letterboxTop`
- `letterboxBottom`

### `AudioFrame`

Current serialized fields:
- `uuid`
- `parentUuid`
- `creationTsNs`
- `originalChannels`
- `originalFrequency`
- `originalSampleCount`

Current omission:
- `cutLeftSampleCount`
- `cutRightSampleCount`

Those members exist in `Perception.h` but are not currently serialized.

### `SegmentationMap`

Fields:
- `uuid`
- `parentUuid`
- `creationTsNs`
- `bitmap`

Bitmap fields:
- `width`
- `height`
- `type`
- `encoding`

When `encoding` is not null, bitmap may also include:
- `raw_size`
- `compressed_size`
- `data_b64`

Encoding values:
- `null` when empty
- `"base64"`
- `"zlib+base64"`

### `TrackTrace`

Fields:
- `uuid`
- `parentUuid`
- `creationTsNs`
- `trackId`
- `points`

Each point:
- `x`
- `y`

### `ObjectEmbedding`

Fields:
- `uuid`
- `parentUuid`
- `creationTsNs`
- `values`

### `PersonClassification`

Fields:
- `uuid`
- `parentUuid`
- `creationTsNs`
- `yesConfidence`
- `noConfidence`

## Gaze-Specific Notes

This section matters for any application using gaze.

### Current parser assumption

Current parser:
- `development/ops-std/postproc/GazeDetectionParser.cpp`

It currently assumes:
- two output tensors
- each shape `[1, 90]`
- one for yaw logits
- one for pitch logits

Current conversion:
- softmax over logits
- expected bin index
- `angle_deg = expected_index * 4 - 180`

This is a Gaze360/L2CS-style assumption.

Important implication:
- output degree range is effectively bin centers from `-180` to `176` in steps of `4`
- not an exact closed `[-180, 180]` range

### Degrees vs radians

Current stored and serialized values:
- degrees

Only `amposd` converts to radians internally for drawing.

### Current OSD interpretation

Current OSD:
- `development/elements/amposd/amposd.cpp`

Current 2D arrow mapping uses:

```text
x = sin(yaw) * cos(pitch)
y = -sin(pitch)
```

where:
- input yaw/pitch are degrees, converted to radians internally
- `y` is negated for image-space screen coordinates where positive Y goes downward

Practical screen interpretation:
- positive yaw draws right
- positive pitch draws up

### Parent relationship for gaze

`eyeYawPitch` detections are expected to be associated with a parent `humanFace` rectangle using `parentUuid`.

If your application wants to anchor gaze to a face:
1. find `YawPitch` with `contentType == "eyeYawPitch"`
2. read `parentUuid`
3. look up `Rect` in `humanFace` with matching `uuid`

## Serializer Details That Matter

Current serializer file:
- `development/common/pek/PerceptionSerializer.cpp`

Important current details:
- layer field is `"infer-id"`
- transport top-level is `{ "frame_counter": ..., "perception": ... }`
- inner `perception` payload is `{ "perfdata": ..., "layers": ... }`
- `YawPitch` values are serialized directly as stored
- `Rect.text` is omitted when empty

Applications should not assume:
- all possible `Perception.h` fields are serialized
- field names match C++ member names exactly

Use the schemas:
- `metadata/api/perception-message.schema.json`
- `metadata/api/perception.schema.json`

## Typical Integration Patterns

### Minimal logging/debug app

Good for:
- validating transport
- validating model output exists
- inspecting field presence

Use:
- `apps/tcp_dump/dump_stream.py`

### Browser dashboard or game

Recommended pattern:
1. connect to `pekcomm` WebSocket
2. parse each transport message JSON object
3. read `message.perception`
4. if `perception` is null, skip or show empty state
5. derive app-level state from `contentType` and detection `type`
6. render UI or gameplay from derived state

If video is also needed:
1. keep `ampsink` intact for WebRTC/browser playback
2. connect to metadata separately via `pekcomm` WebSocket

### Event-driven app

Recommended pattern:
1. connect to TCP JSON stream
2. parse each transport message
3. read `message.perception`
4. if `perception` is null, continue
5. group by `contentType`
6. map detections into app-specific events/state

Examples:
- latest gaze direction per face
- camera-contact boolean per face
- object presence/tracking state

### State cache pattern

Good for interactive apps and games.

Maintain:
- latest frame timestamp
- latest face set by `uuid`
- latest gaze by face `uuid`
- latest contact state by face `uuid`

Do not rely on:
- detection array order

Do rely on:
- `uuid`
- `parentUuid`

## Robustness Rules For Agents

When building an app, agents should follow these rules:

1. Validate transport messages against `metadata/api/perception-message.schema.json` when practical.
2. Validate non-null payloads against `metadata/api/perception.schema.json` when practical.
3. Ignore unknown `contentType` values instead of crashing.
4. Ignore unknown detection `type` values instead of crashing.
5. Treat `perfdata` as debug-only text.
6. Treat `Rect.text` as optional.
7. Treat `AudioFrame` cut-sample fields as unavailable in JSON unless serializer changes.
8. Use `parentUuid` joins instead of positional assumptions.
9. Assume multiple layers and multiple detections per message.
10. Assume messages may arrive continuously and indefinitely.
11. Treat transport disconnects as recoverable.

## Recommended App-Level Abstractions

If building arbitrary applications, map raw metadata into simpler app-facing types.

Suggested internal app models:
- `FaceObservation`
- `GazeObservation`
- `CameraContactObservation`
- `ObjectObservation`
- `SegmentationObservation`

Example `FaceObservation`:
- `face_uuid`
- `bbox`
- `created_ts_ns`

Example `GazeObservation`:
- `face_uuid`
- `yaw_deg`
- `pitch_deg`
- `confidence`
- `created_ts_ns`

Example `CameraContactObservation`:
- `face_uuid`
- `has_camera_contact`
- `confidence`
- `created_ts_ns`

This lets app logic stay independent of the raw JSON shape.

## Known Limitations

Important current limitations:
- no explicit schema/version field inside each message
- no schema version field in the transport envelope
- no message ordering field beyond `frame_counter`
- transport-specific connection details are outside the JSON payload
- no stable product-level API guarantee
- no explicit formal spec for all coordinate conventions beyond code/docs
- gaze parser currently assumes logits output format for the checked-in model path

## If You Need To Change The Contract

To add a new metadata result cleanly:

1. add a new struct in `development/common/pek/Perception.h`
2. add serializer support in `development/common/pek/PerceptionSerializer.h`
3. add serializer implementation in `development/common/pek/PerceptionSerializer.cpp`
4. update `metadata/api/perception.schema.json`
5. update `metadata/api/perception-message.schema.json` if the transport envelope changes
6. update this file: `metadata/AGENTS.md`
7. update `metadata/api/README.md` if transport or schema usage changes
8. update or add example apps under `apps/`

## Fast Start Checklist

For a new app agent:

1. Read `metadata/AGENTS.md`.
2. Read `metadata/api/perception-message.schema.json`.
3. Read `metadata/api/perception.schema.json`.
4. Use `metadata/api/json_stream.py` as the transport client.
5. Connect to either:
   - TCP `host:7001`
   - WebSocket `ws://host:8002/ws`
6. Parse each transport message as one transport envelope object:
   - one line for TCP
   - one text frame for WebSocket
7. Read `message.perception` and handle `null` safely.
8. Dispatch by `layer.contentType` and detection `type`.
9. Join related objects using `uuid` and `parentUuid`.
10. Convert raw metadata into app-specific state/events.
11. Handle unknown fields/types gracefully and do not assume undocumented semantics.
