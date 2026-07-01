## Metadata API

This directory defines the current external JSON contract for AMP metadata transport and payloads.

## Canonical contract split

Current `pekcomm` transport messages are JSON objects shaped like:

```json
{
  "frame_counter": 0,
  "perception": {
    "perfdata": [],
    "layers": []
  }
}
```

The contract is intentionally split into:

- `perception-message.schema.json` for the **transport message** emitted by `development/elements/pekcomm/writer.cpp`
- `perception.schema.json` for the inner serialized `pek::Perception` payload emitted by `development/common/pek/PerceptionSerializer.cpp`

This split matters because:

- the transport adds `frame_counter`
- the transport can emit `"perception": null`
- the inner payload still has its own stable repo-local structure

## Current transport details

- `frame_counter` is transport metadata added by `pekcomm`
- `perception` is the serialized `pek::Perception` object when metadata is present
- TCP framing is newline-delimited JSON messages
- WebSocket framing is one JSON transport message per text frame

## Inner `Perception` payload details

`perception.schema.json` does not attempt to mirror `development/common/pek/Perception.h` exactly.
It reflects the actual serialized payload shape.

Important current serializer details reflected by the schema:

- layer field name is `"infer-id"`, not `inferElementId`
- `AudioFrame` currently serializes `originalChannels`, `originalFrequency`, and `originalSampleCount`, but not `cutLeftSampleCount` or `cutRightSampleCount`
- `Rect.text` is omitted when empty
- `Bitmap.encoding` can be `null` when the bitmap has no pixels

## Primary files

- `perception-message.schema.json`
- `perception.schema.json`
- `json_stream.py`

## Python helper

`json_stream.py` provides a thin reusable TCP client for newline-delimited JSON transport messages.

Current message access pattern:

```python
from metadata.api import JsonStreamClient

with JsonStreamClient(host="127.0.0.1", port=7001) as client:
    for message in client.iter_messages():
        perception = message.get("perception")
        if perception is None:
            continue
        print(perception["layers"])
```

## Example apps

- `apps/tcp_dump`
- `apps/metadata_browser`

Run the TCP dumper with:

```bash
python3 apps/tcp_dump/dump_stream.py --host 127.0.0.1 --port 7001
```
