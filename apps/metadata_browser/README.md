# Metadata Browser Starter

This is the checked-in browser starter template for AMP metadata consumers.

It connects to the `pekcomm` WebSocket metadata stream and demonstrates:
- WebSocket connect and disconnect
- transport-message normalization
- safe handling of `message.perception == null`
- high-level transport and payload summaries
- safe traversal of layers and detections
- raw JSON inspection for the latest message

Read these first:
- `metadata/AGENTS.md`
- `metadata/api/README.md`
- `metadata/api/perception.schema.json`

## Run

Serve the directory with a static file server from the repository root:

```bash
python3 -m http.server 8088
```

Then open:

```text
http://127.0.0.1:8088/apps/metadata_browser/
```

Default WebSocket endpoint:

```text
ws://127.0.0.1:8002/ws
```

If your stream is exposed elsewhere, change the URL in the app header before connecting.

## Notes

- This app is intentionally generic and should be the starting point for browser metadata apps under `apps/`.
- It expects the current `pekcomm` transport message shape:
  - `{ "frame_counter": <number>, "perception": { ... } }`
- It also tolerates a bare `Perception` object for compatibility with older assumptions and local experiments.
- It does not impose app-specific control logic, coordinate mapping, or interpretation beyond safe metadata traversal.
