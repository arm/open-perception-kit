<!-- SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates -->

# Python SDK Guide

The Python SDK is generated into `generated/python`. Endpoint clients use the
packet-owning API:

```python
from metapoc_python.packet import Envelope
```

Embedded guest scripts use the separate `metapoc_python.guest` API documented in
[C++ Python Bridge Guest Script API](cpp-python-bridge.md). Both APIs reuse the
same generated `*T` payload classes. That guide uses the default `metapoc`
public name; pass `--public-name metapoc_python` when generating its C++ bridge
to use the Python package shown here.

The generated source tree is wheel-ready. Its `pyproject.toml` includes
`guest.pyi` and `py.typed` as package data, so an installed wheel provides guest
API completion in editors even though importing `metapoc_python.guest` still requires
the registered C++ bridge at runtime.

Known payload ids are generated internally and are not part of the public API.
Known payload operations use generated FlatBuffers object API classes such as
`PerceptionT` and `TelemetryT`.

## Generate And Install The SDK

The generated Python package requires Python 3.10 or newer.

```bash
python3 tools/flowdata/gen.py generate \
  --name metapoc \
  --public-name metapoc_python \
  --version 1.2.3 \
  --sdk python \
  --flatc "$(which flatc)" \
  --schema-dir payloads
```

```bash
pip install -e generated/python
```

Omit `--public-name` when the SDK name should match the schema namespace
supplied by `--name`. The public name applies to every generated language.

The package metadata and runtime constant use the supplied release version:

```python
import metapoc_python

assert metapoc_python.METAPOC_PYTHON_VERSION == "1.2.3"
assert metapoc_python.__version__ == metapoc_python.METAPOC_PYTHON_VERSION
assert metapoc_python.FLATBUFFERS_VERSION_REQUIREMENT == ">=24.3.25,<26.0.0"
```

The generated package declares the same FlatBuffers runtime range in
`pyproject.toml`, so package installation enforces the supported dependency.

## Important Generated Names

```python
from metapoc_python.packet import Envelope
from metapoc_python.fb.demo.common.BoundingBox import BoundingBoxT
from metapoc_python.fb.demo.common.GeoPoint import GeoPointT
from metapoc_python.fb.demo.common.Vector3 import Vector3T
from metapoc_python.fb.demo.perception.Perception import PerceptionT
from metapoc_python.fb.demo.telemetry.Telemetry import TelemetryT
```

The `*T` classes are FlatBuffers object API native classes. Field names follow
the FlatBuffers Python generator style, such as `sensorId`, `frameId`, and
`timestampMs`.

## Build A Packet

```python
from metapoc_python.fb.demo.perception.Perception import PerceptionT
from metapoc_python.fb.demo.telemetry.Telemetry import TelemetryT
from metapoc_python.packet import Envelope

front = PerceptionT()
front.sensorId = "front_camera"
front.frameId = 4821
front.timestampMs = 1714231234123
front.detections = []

rear = PerceptionT()
rear.sensorId = "rear_camera"
rear.frameId = 4822
rear.timestampMs = 1714231234124
rear.detections = []

telemetry = TelemetryT()
telemetry.vehicleId = "demo-vehicle-01"
telemetry.sequence = 9012
telemetry.timestampMs = 1714231234125

envelope = Envelope()
envelope.add(front)
envelope.add(rear)
envelope.add(telemetry)
packet = envelope.serialize()
```

The two `PerceptionT` values become two entries with the same known payload
type. Read them back with occurrence indexes `0` and `1`.

## Read A Packet

```python
from metapoc_python.fb.demo.perception.Perception import PerceptionT
from metapoc_python.fb.demo.telemetry.Telemetry import TelemetryT
from metapoc_python.packet import Envelope

envelope = Envelope(packet)
if not envelope.valid():
    print(envelope.error())
    raise RuntimeError("invalid packet")

front = envelope.get(PerceptionT, 0)
rear = envelope.get(PerceptionT, 1)
telemetry = envelope.get(TelemetryT, 0)

assert envelope.count(PerceptionT) == 2
assert envelope.contains(TelemetryT)
```

`get()` returns a generated FlatBuffers object API native object. The SDK does
not expose raw FlatBuffers table objects or known-payload blobs through the
public API.

## External And Unknown Payloads

External payloads are caller-owned opaque bytes addressed by `ExternalKey`
handles. The SDK stores the handle as a high-bit `uint64` id internally, but
public envelope APIs do not accept bare strings or integers for external
payload access:

```python
from metapoc_python.packet import external_key

tracks_key = external_key("com.example.tracker.tracks")
envelope.add(tracks_key, b"...")
payload = envelope.get(tracks_key)
```

Python can also parse and reserialize envelopes that contain low-domain payload
ids not known by the generated SDK, or known ids whose blobs do not verify as
the expected type. Those entries are private preserve-only data and remain in
the packet when `serialize()` is called. Typed `count()`, `contains()`, `get()`,
and `for_each()` only include entries that successfully decode as the requested
type.

## API Reference

State:

- `valid() -> bool`
- `error() -> str | None`
- `empty() -> bool`
- `size() -> int`

Known payloads and external payloads:

- `add(native_object)`: append a known generated object-api payload
- `add(external_key, blob)`: append external opaque bytes
- `count(payload_type | external_key) -> int`
- `contains(payload_type | external_key) -> bool`
- `get(payload_type | external_key, index=0)`
- `for_each(payload_type | external_key) -> Iterator[Any | bytes]`

External payloads:

- `external_key(key: str) -> ExternalKey`
- `is_external_key(key: object) -> bool`

Serialization:

- `serialize() -> bytes`

Producer identity:

- `producer_sdk_name: str`
- `producer_sdk_version: str`
- `producer_schema_set_sha256: str`
- `producer_identity() -> ProducerIdentityStatus`

Import `ProducerIdentityStatus` from `metapoc_python.packet`. Parsing does not reject a
missing or mismatched identity. `serialize()` stamps the current generated SDK
identity, including when forwarding an envelope produced by another SDK.

`Envelope(packet)` checks the outer envelope file identifier and parses the
outer packet through generated FlatBuffers accessors. It does not provide
verifier-equivalent structural validation, so boundary code should still apply
practical size and trust limits. Payload compatibility is checked per payload id
and blob when typed APIs are called: entries that do not decode through the typed
API are preserved internally for roundtrip serialization. If an envelope is
invalid, `add()` and `serialize()` raise `RuntimeError`.

`get()` and `for_each()` return immutable `bytes` values for `ExternalKey`
selectors. The SDK does not expose mutable views into stored external blobs.
