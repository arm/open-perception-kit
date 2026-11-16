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

# Python SDK Guide

The Python SDK is generated into `generated/python`. Endpoint clients use the
packet-owning API:

```python
from metapoc.packet import Envelope
```

Embedded guest scripts use the separate `metapoc.guest` API documented in
[C++ Python Bridge Guest Script API](cpp-python-bridge.md). Both APIs reuse the
same generated `*T` payload classes.

The generated source tree is wheel-ready. Its `pyproject.toml` includes
`guest.pyi` and `py.typed` as package data, so an installed wheel provides guest
API completion in editors even though importing `metapoc.guest` still requires
the registered C++ bridge at runtime.

Known payload ids are generated internally and are not part of the public API.
Known payload operations use generated FlatBuffers object API classes such as
`PerceptionT` and `TelemetryT`.

## Generate And Install The SDK

The generated Python package requires Python 3.10 or newer.

```bash
python3 tools/flowdata/gen.py generate \
  --name metapoc \
  --version 1.2.3 \
  --sdk python \
  --flatc "$(which flatc)" \
  --schema-dir payloads
```

```bash
python3 tools/flowdata/gen.py verify-manifest generated/python/flowdata-manifest.json --schema-root payloads
python3 -m pip install -e generated/python
```

Use a Python >=3.10 environment. Verify before installation/imports add package
metadata or bytecode caches to the generated SDK root.

The package metadata and runtime constant use the supplied release version:

```python
import metapoc

print("SDK version:", metapoc.METAPOC_VERSION)
print("FlatBuffers requirement:", metapoc.FLATBUFFERS_VERSION_REQUIREMENT)
```

The generated package declares the same FlatBuffers runtime range in
`pyproject.toml`, so package installation enforces the supported dependency.

## Important Generated Names

```python
from metapoc.packet import Envelope
from metapoc.fb.demo.common.BoundingBox import BoundingBoxT
from metapoc.fb.demo.common.GeoPoint import GeoPointT
from metapoc.fb.demo.common.Vector3 import Vector3T
from metapoc.fb.demo.perception.Perception import PerceptionT
from metapoc.fb.demo.telemetry.Telemetry import TelemetryT
```

The `*T` classes are FlatBuffers object API native classes. Field names follow
the FlatBuffers Python generator style, such as `sensorId`, `frameId`, and
`timestampMs`.

## Build A Packet

```python
from metapoc.fb.demo.perception.Perception import PerceptionT
from metapoc.fb.demo.telemetry.Telemetry import TelemetryT
from metapoc.packet import Envelope

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
from metapoc.fb.demo.perception.Perception import PerceptionT
from metapoc.fb.demo.telemetry.Telemetry import TelemetryT
from metapoc.packet import Envelope, ProducerIdentityStatus

envelope = Envelope(packet)
if not envelope.valid():
    print(envelope.error())
    raise RuntimeError("invalid packet")

# This application accepts the publisher's same-major wire contract.
if envelope.producer_identity() not in (
    ProducerIdentityStatus.EXACT_MATCH,
    ProducerIdentityStatus.COMPATIBLE_MATCH,
):
    raise ValueError("unsupported producer SDK")

front = envelope.get(PerceptionT, 0)
rear = envelope.get(PerceptionT, 1)
telemetry = envelope.get(TelemetryT, 0)

print("Perception payloads:", envelope.count(PerceptionT))
print("Has telemetry:", envelope.contains(TelemetryT))
```

`get()` returns a generated FlatBuffers object API native object. The SDK does
not expose raw FlatBuffers table objects or known-payload blobs through the
public API.

FlatBuffers object-API values retain the generated runtime's conventions: strings
read from packets may be `bytes`, and absent vectors may be `None`. This differs
from bridge proxies, whose string getters return Python `str` values.

## External And Unknown Payloads

External payloads are caller-owned opaque bytes addressed by `ExternalKey`
handles. The SDK stores the handle as a high-bit `uint64` id internally, but
public envelope APIs do not accept bare strings or integers for external
payload access:

```python
from metapoc.packet import external_key

tracks_key = external_key("com.example.tracker.tracks")
envelope.add(tracks_key, b"...")
payload = envelope.get(tracks_key)
```

Python can also parse and reserialize envelopes that contain low-domain payload
ids not known by the generated SDK, or known ids whose blobs do not decode as
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
- `decode(packet) -> Envelope`: equivalent to constructing `Envelope(packet)`

Producer identity:

- `producer_sdk_name: str`
- `producer_sdk_version: str`
- `producer_schema_set_sha256: str`
- `producer_identity() -> ProducerIdentityStatus`

Import `ProducerIdentityStatus` from `metapoc.packet`. Its values are
`NAME_MISMATCH`, `EXACT_MATCH`, `COMPATIBLE_MATCH`, and `VERSION_MISMATCH`.
Classification checks the SDK name first. With a matching name, identical
versions are exact and different versions sharing a major are compatible,
including major zero. Missing/malformed names or versions
produce their respective mismatch status. Schema hashes do not affect the result.

The result is advisory; publishers own compatibility promises and applications
apply acceptance policy before typed reads. Parsing does not reject a missing
or mismatched identity. `serialize()` stamps the current generated SDK identity,
including when forwarding an envelope produced by another SDK. See the
[compatibility model](integration-guide.md#compatibility-model).

`Envelope(packet)` checks the outer envelope file identifier and parses the
outer packet through generated FlatBuffers accessors. It does not provide
verifier-equivalent structural validation, so boundary code should still apply
practical size and trust limits. Payload selection and decoding are checked per
entry when typed APIs are called: entries that do not decode through the typed
API are preserved internally for roundtrip serialization. If an envelope is
invalid, `add()` and `serialize()` raise `RuntimeError`.

`get()` and `for_each()` return immutable `bytes` values for `ExternalKey`
selectors. The SDK does not expose mutable views into stored external blobs.
