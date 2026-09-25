# C++ Python Bridge Guest Script API

The C++ Python bridge is an optional generated add-on for C++ hosts that embed
CPython. It exposes a temporary live Python object named by the host, usually
`env`, which wraps an existing C++ `container::envelope&`.

This is separate from the generated `metapoc_python.packet` API, which owns serialized
packet bytes. Guest scripts import `Envelope` from `metapoc_python.guest`; the host
supplies the actual C++-owned envelope wrapper for the callback.

The generated Python package includes `metapoc_python/guest.pyi` and `metapoc_python/py.typed`
for editor completion and static analysis. Type checkers read the stub without
executing `metapoc_python.guest`. A normal Python runtime import still raises the
documented error until a C++ host registers `metapoc_python_bridge`.

## Where The API Comes From

The guest script does not import `env`. The C++ host creates a
`python_bridge::scoped_envelope`, obtains its `py_object()`, and injects that
object into the Python globals before executing the script.

The API is implemented as a generated CPython extension type. The generator
source is:

```text
tools/flowdata-sdk/tools/flowdata/engine/cpp/python_bridge.py
```

Generated C++ output is written to:

```text
generated/cpp/python_bridge/<public_name>_python_bridge.h
generated/cpp/python_bridge/<public_name>_python_bridge.cpp
```

The generated method table defines the callable guest API. At runtime, scripts
can inspect it with:

```python
print(type(env))
print(dir(env))
help(env)
help(env.get)
```

## Guest Script Contract

The host provides:

```python
env
```

The script imports the guest API for the envelope type and shared helpers:

```python
from metapoc_python.guest import Envelope, external_key


def process(results: Envelope) -> None:
    ...


process(env)
```

The guest envelope exposes the same read-only producer metadata and
`producer_identity()` status as the packet API. The metadata comes from the input
packet, or defaults to the current C++ SDK identity for a new envelope. Host
serialization stamps the current C++ SDK identity into the output packet without
changing the live envelope's stored metadata.

The script imports generated Python payload classes from the normal Python SDK:

```python
from metapoc_python.fb.demo.perception.Perception import PerceptionT
```

For another SDK name or schema namespace, use that project's generated Python
package and payload classes.

Use CPython 3.10 or newer. The generated Python SDK and
`flatbuffers>=24.3.25,<26.0.0` must be importable by the embedded interpreter,
including for read-only guest scripts. Known-payload appends also use the
FlatBuffers builder to pack the Python object-api value.

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
from metapoc_python.fb.demo.perception.Perception import PerceptionT

if env.contains(PerceptionT):
    first = env.get(PerceptionT)
    print(first.sensorId)

for perception in env.for_each(PerceptionT):
    print(perception.frameId)
```

`get()` returns `None` when the requested occurrence is missing or cannot be
decoded as the requested generated payload type. `index` is zero-based and must
be a non-negative integer.

`for_each()` returns a Python list snapshot of matching read-only proxies in
envelope order.

## Known Payload Proxies

Known reads return read-only proxy objects backed by C++ `payload_ref<T>`
handles. They do not copy the whole native payload for each read.

Proxy field names follow the generated Python FlatBuffers object API naming.
Nested tables and vectors are exposed as read-only proxies/sequences. Scalar
and string fields are returned as normal Python values.

Existing entries cannot be mutated:

```python
payload = env.get(PerceptionT)
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
from metapoc_python.fb.demo.common.BoundingBox import BoundingBoxT
from metapoc_python.fb.demo.perception.Detection import DetectionT
from metapoc_python.fb.demo.perception.Perception import PerceptionT

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
from metapoc_python.guest import external_key

key = external_key("com.example.bridge.demo")

env.add(key, b"bridge-demo-bytes")

assert env.contains(key)
assert env.count(key) == 1
assert env.get(key) == b"bridge-demo-bytes"

for blob in env.for_each(key):
    print(len(blob))
```

`add()` accepts Python bytes-like objects through the buffer protocol when the
first argument is an `ExternalKey`, for example `bytes`, `bytearray`, or
`memoryview`. The data is copied into envelope-owned storage. `get()` and
`for_each()` return `bytes` values for `ExternalKey` selectors.

## Wrapper Validity And Lifetime

The C++ host owns the envelope and the bridge wrapper lifetime.

```python
env.valid() -> bool
```

`env.valid()` reports whether the live wrapper is active. After the host ends
the `python_bridge::scoped_envelope` scope, it returns `False`; other envelope
operations and producer-metadata access raise `RuntimeError`.

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
from metapoc_python.guest import Envelope, external_key
from metapoc_python.fb.demo.perception.Perception import PerceptionT


def main(results: Envelope) -> None:
    assert results.valid()

    for payload in results.for_each(PerceptionT):
        print(payload.sensorId, payload.frameId)

    payload = PerceptionT()
    payload.sensorId = "python"
    payload.frameId = 1
    results.add(payload)

    key = external_key("com.example.bridge.demo")
    results.add(key, b"bytes")
    assert results.get(key) == b"bytes"


main(env)
```

The repository demo script is:

```text
demo/python_bridge/live_demo.py
```
