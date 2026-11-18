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

# C++ Python Bridge Guest Script API

The C++ Python bridge is an optional generated add-on for C++ hosts that embed
CPython. It exposes a temporary live Python object named by the host, usually
`env`, which wraps an existing C++ `container::envelope&`.

This is separate from the generated `metapoc.packet` API, which owns serialized
packet bytes. Guest scripts import `Envelope` from `metapoc.guest`; the host
supplies the actual C++-owned envelope wrapper for the callback.

The generated Python package includes `metapoc/guest.pyi` and `metapoc/py.typed`
for editor completion and static analysis. Type checkers read the stub without
executing `metapoc.guest`. A normal Python runtime import still raises the
documented error until a C++ host registers `metapoc_bridge`.

## Where The API Comes From

The guest script does not import `env`. The C++ host creates a
`python_bridge::scoped_envelope`, obtains its `py_object()`, and injects that
object into the Python globals before executing the script.

The API is implemented as a generated CPython extension type. The stable
generator entrypoint is:

```text
tools/flowdata/engine/cpp/python_bridge.py
```

Parsing, emission and runtime templates live in the sibling `bridge_*.py`
modules. The method table is in `bridge_module_template.py`; see the
[generator architecture](../tools/flowdata/ARCHITECTURE.md#c-python-bridge).

Generated C++ output is written to:

```text
generated/cpp/python_bridge/<sdk>_python_bridge.h
generated/cpp/python_bridge/<sdk>_python_bridge.cpp
```

The generated method table defines the callable guest API. At runtime, scripts
can inspect it with:

```python
print(type(env))
print(dir(env))
help(env)
help(env.get)
```

## Run The Bridge Demo

Run from the repository root with supported `flatc`, matching C++ headers,
CMake >=3.18, a C++20 compiler, and Python >=3.10 development/embed support. Activate a
Python environment with `flatbuffers>=24.3.25,<26.0.0` installed. The path lookup
below uses that environment's `python3`:

```bash
python3 tools/flowdata/gen.py generate --name metapoc --version 1.2.3 --sdk cpp --cpp-python-bridge --cmake --meson --schema-dir payloads
python3 tools/flowdata/gen.py generate --name metapoc --version 1.2.3 --sdk python --schema-dir payloads
python3 tools/flowdata/gen.py verify-manifest generated/cpp/flowdata-manifest.json --schema-root payloads
python3 tools/flowdata/gen.py verify-manifest generated/python/flowdata-manifest.json --schema-root payloads

FLATBUFFERS_PYTHON_PATH="$(python3 -c 'import pathlib, flatbuffers; print(pathlib.Path(flatbuffers.__file__).resolve().parent.parent)')"
cmake -S demo/cpp/cmake -B demo/cpp/cmake/build \
  -DMETAPOC_PYTHON_BRIDGE_EXTRA_PATH="$FLATBUFFERS_PYTHON_PATH"
cmake --build demo/cpp/cmake/build --target python_bridge_client
./demo/cpp/cmake/build/python_bridge_client
```

The C++ host creates an in-memory envelope containing one sample `PerceptionT`
(`cpp-seed-camera`, frame 42). It injects the live wrapper as `env` and runs
`demo/python_bridge/live_demo.py`. Python reads it and appends another perception
and external bytes. C++ then prints `After Python: perception=2, external=1`.
No input envelope file is needed and the whole envelope is not serialized at
the callback boundary.

To use another script, run
`./demo/cpp/cmake/build/python_bridge_client path/to/script.py`.
The argument selects the script, not packet input. Do not run the guest script
directly with `python3`; it requires the host-injected `env`. For ad-hoc executable
signing on managed macOS, see `tests/README.md` in a full repository checkout.

## Guest Script Contract

The host provides:

```python
env
```

The script imports the guest API for the envelope type and shared helpers:

```python
from metapoc.guest import Envelope, external_key


def process(results: Envelope) -> None:
    ...


process(env)
```

The guest envelope exposes the same read-only producer metadata and
`producer_identity()` status as the packet API. The metadata comes from the input
packet, or defaults to the current C++ SDK identity for a new envelope. Host
serialization stamps the current C++ SDK identity into the output packet without
changing the live envelope's stored metadata.

`ProducerIdentityStatus` exposes `NAME_MISMATCH`, `EXACT_MATCH`,
`COMPATIBLE_MATCH`, and `VERSION_MISMATCH`. Classification compares the recorded
producer name/version against the host C++ SDK, not the Python package version.
With a matching name, identical versions are exact; differing versions sharing
a major are compatible, including major zero, just as in the other SDKs.
The schema hash is diagnostic only. Scripts and hosts decide how to apply the
advisory result; see the [compatibility model](integration-guide.md#compatibility-model).

The script imports generated Python payload classes from the normal Python SDK:

```python
from metapoc.fb.demo.perception.Perception import PerceptionT
```

For another SDK name or schema namespace, use that project's generated Python
package and payload classes.

Use CPython 3.10 or newer. Both the generated Python SDK and its
`flatbuffers>=24.3.25,<26.0.0` dependency must be
importable by the embedded interpreter, including for read-only scripts:
`<sdk>.guest` imports shared helpers from `<sdk>.sdk`, which imports `flatbuffers`.
Known appends additionally use that runtime to pack the Python object-api value
before appending it to the C++ envelope.

## Known Payload Reads

Known payloads are accessed by generated Python payload type, not by payload id.

```python
env.count(PayloadT) -> int
env.contains(PayloadT) -> bool
env.get(PayloadT, index=0) -> PayloadProxy | None
env.for_each(PayloadT) -> list[PayloadProxy]
```

Example:

```python
from metapoc.fb.demo.perception.Perception import PerceptionT

if env.contains(PerceptionT):
    first = env.get(PerceptionT)
    print(first.sensorId)

for perception in env.for_each(PerceptionT):
    print(perception.frameId)
```

`get()` returns `None` when the requested occurrence is missing. Indexes count
successfully decoded matching entries; undecodable entries are skipped. `index`
is zero-based and must be a non-negative integer.

`for_each()` returns a Python list snapshot of matching read-only proxies in
envelope order.

## Known Payload Proxies

Known reads return read-only proxy objects backed by C++ `payload_ref<T>`
handles. They do not copy the whole native payload for each read.

Proxy field names follow the generated Python FlatBuffers object API naming.
For example, a schema field named `class` is exposed as `class_`; C++ member
escaping is handled independently by the bridge.
Nested tables and vectors are exposed as read-only proxies/sequences. Scalar
and string fields are returned as normal Python values.
Optional scalar fields (`= null`) return `None` when absent and their scalar
value when present, including `False` and zero.

Existing entries cannot be mutated:

```python
payload = env.get(PerceptionT)
if payload is not None:
    payload.sensorId = "new-value"  # raises AttributeError
```

To change data logically, append a new payload entry with `env.add(...)`.

## Known Payload Appends

Append known payloads by passing a generated Python FlatBuffers object API
`*T` value:

```python
env.add(payload_t) -> None
```

Example:

```python
from metapoc.fb.demo.common.BoundingBox import BoundingBoxT
from metapoc.fb.demo.perception.Detection import DetectionT
from metapoc.fb.demo.perception.Perception import PerceptionT

box = BoundingBoxT()
box.x, box.y, box.w, box.h = 5.0, 6.0, 7.0, 8.0
detection = DetectionT()
detection.trackId = 8
detection.classId = 1
detection.confidence = 0.5
detection.bbox = box

payload = PerceptionT()
payload.sensorId = "python-camera"
payload.frameId = 43
payload.timestampMs = 5678
payload.detections = [detection]
env.add(payload)
```

Internally, the bridge packs that single Python object with the Python
FlatBuffers builder, verifies the expected C++ root type, unpacks it into the
C++ native `T`, and appends it to the live envelope.

## External Payloads

External payloads are opaque caller-owned bytes addressed by an `ExternalKey`
handle.

```python
env.count(key) -> int
env.contains(key) -> bool
env.get(key, index=0) -> bytes | None
env.for_each(key) -> list[bytes]
env.add(key, bytes_like) -> None
```

Create the key with the generated Python SDK helper. Bare strings and numeric
ids are rejected by envelope methods.

Example:

```python
from metapoc.guest import external_key

key = external_key("com.example.bridge.demo")

env.add(key, b"bridge-demo-bytes")

print("External payloads:", env.count(key))
print("First external payload:", env.get(key))

for blob in env.for_each(key):
    print(len(blob))
```

`add()` accepts Python bytes-like objects through the buffer protocol when the
first argument is an `ExternalKey`, for example `bytes`, `bytearray`, or
`memoryview`. The data is copied into envelope-owned storage. `get()` and
`for_each()` return `bytes` values for `ExternalKey` selectors.
If the native envelope rejects an append, the bridge raises `RuntimeError` and
releases the input buffer before returning to Python.

## Wrapper Validity And Lifetime

The C++ host owns the envelope and the bridge wrapper lifetime.

```python
env.valid() -> bool
```

`env.valid()` checks wrapper lifetime, not the outer wire validity of the wrapped
C++ envelope. The host should validate parsed envelopes before exposing them.
After the host ends the `python_bridge::scoped_envelope` scope, `env.valid()`
returns `False`; payload methods and producer-metadata access raise `RuntimeError`.

Payload proxies already returned from `env.get(...)` keep their referenced C++
payload entry alive through `payload_ref<T>` anchors, but they are still
read-only. The host must keep the C++ envelope alive while the live wrapper is
valid.

The bridge does not add stronger thread-safety than the C++ envelope. The host
must manage callback and thread lifetimes around the envelope object.

## Unsupported Operations

Guest scripts must not expect any API for:

- known payload ids,
- raw known-payload FlatBuffers blobs,
- mutable access to existing payloads,
- edit, erase, replace, or upsert,
- arbitrary numeric external ids.

The bridge is intentionally append/read only.

## Minimal Complete Script

```python
from metapoc.guest import Envelope, ProducerIdentityStatus, external_key
from metapoc.fb.demo.perception.Perception import PerceptionT


def main(results: Envelope) -> None:
    if not results.valid():
        raise RuntimeError("host envelope is no longer available")
    if results.producer_identity() not in (
        ProducerIdentityStatus.EXACT_MATCH,
        ProducerIdentityStatus.COMPATIBLE_MATCH,
    ):
        raise ValueError("unsupported producer SDK")

    for payload in results.for_each(PerceptionT):
        print(payload.sensorId, payload.frameId)

    payload = PerceptionT()
    payload.sensorId = "python"
    payload.frameId = 1
    results.add(payload)

    key = external_key("com.example.bridge.demo")
    results.add(key, b"bytes")
    print(results.get(key))


main(env)
```

The repository demo script is:

```text
demo/python_bridge/live_demo.py
```

It is a usage example. Bridge regression checks have a dedicated host in
`tests/cpp/python_bridge_test.cpp` and guest script in `tests/python/guest_test.py`.
See `tests/README.md` in a full checkout for the separate test commands; tests
and demos are not included in the generator source package.
