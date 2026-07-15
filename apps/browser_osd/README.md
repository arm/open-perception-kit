# Browser OSD

Static browser app that receives PEK video through `peksink` WebRTC and renders PEK metadata from `pekcomm` WebSocket onto a canvas overlay.

## Run

Serve this app directory:

```bash
cd apps/browser_osd
python3 -m http.server 8088
```

Open:

```text
http://127.0.0.1:8088/
```

You can also serve the repository root and open `http://127.0.0.1:8088/apps/browser_osd/`.

Default endpoints:

```text
WebRTC signaling: ws://127.0.0.1:8000/ws
Metadata:         ws://127.0.0.1:8002/ws
```

The endpoint fields are editable in the app. Some checked-in pipelines use metadata port `7001` or `8080`; match the field to the active `pekcomm ws-port`.

## Pipeline Shape

Use a pipeline that publishes metadata before the sink. For browser-only OSD, disable server-side OSD to avoid drawing overlays twice:

```text
... ! pekcomm method=websocket ws-port=8002 endpoint=/ws ! pekosd enabled=false ! peksink name=sink
```

Existing pipelines with `pekosd enabled=true` still work, but the WebRTC video already contains the GStreamer-side overlay.

## Metadata Contract

The app expects one JSON transport message per WebSocket text frame:

```json
{
  "frame_counter": 0,
  "perception": {
    "perfdata": [],
    "layers": []
  }
}
```

It follows the repo-local schemas in `metadata/api/perception-message.schema.json` and `metadata/api/perception.schema.json`. `perception: null`, unknown `contentType` values, and unknown detection `type` values are ignored safely.

## Coordinate Policy

Coordinates are mapped into the displayed video rectangle on the page. The mapper prefers a serialized `VideoFrame.originalWidth` and `VideoFrame.originalHeight` when available, then falls back to `video.videoWidth` and `video.videoHeight`. The video is displayed with `object-fit: contain`, so the canvas accounts for letterboxing.

## Rendered OSD

Implemented in `osd-renderer.js`:

- `genericObject` `Rect`: boxes and labels
- `humanFace` `Rect`: face circles
- `eyeYawPitch` `YawPitch`: gaze arrows anchored through `parentUuid`
- `cameraContact` `Classification`: green/red face marker
- `trackTrace` `TrackTrace`: fading track segments
- `classification` `Classification`: lower-left ranked labels
- `personClassification` `PersonClassification`: centered status text
- `perfdata`: top-left monospace overlay

Segmentation maps and browser-side background replacement are not implemented in this first version.
