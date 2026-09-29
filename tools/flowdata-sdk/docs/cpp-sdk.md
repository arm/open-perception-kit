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

# C++ SDK Guide

The C++ SDK is generated into `generated/cpp`. Applications include one public
header:

```cpp
#include "metapoc.h"
```

The main type is:

```cpp
metapoc::container::envelope
```

The primary header also exposes the supplied release version through a constant
derived from `--name`. The FlatBuffers assertion below assumes generation with
`flatc` 25.9.23:

```cpp
static_assert(metapoc::METAPOC_VERSION == "1.2.3");
static_assert(metapoc::METAPOC_FLATBUFFERS_VERSION_REQUIREMENT == "==25.9.23");
```

The generated CMake module exposes the same value as `METAPOC_VERSION`; the
generated Meson module exposes it as `metapoc_version`. The FlatBuffers
requirement is the exact compiler version because upstream generated C++
headers enforce an exact header/compiler match. CMake exposes it as
`METAPOC_FLATBUFFERS_VERSION_REQUIREMENT`; Meson exposes it as
`metapoc_flatbuffers_version_requirement`.

CMake also exposes `METAPOC_PYTHON_VERSION_REQUIREMENT`,
`METAPOC_PYTHON_BRIDGE_AVAILABLE`, and `METAPOC_PYTHON_BRIDGE_MODULE`. Meson
exposes the equivalent `metapoc_python_version_requirement`,
`metapoc_python_bridge_available`, and `metapoc_python_bridge_module` values.
These descriptors let consuming projects configure bridge hosts without
reconstructing generator naming conventions.

The envelope is append-only. It stores ordered payload entries, allows multiple
entries with the same known payload type, and reads known payloads by native
type plus occurrence index. Known payload ids are generated internally and are
not part of the public API. Known payloads appended through C++ are kept as
native object API values until `serialize()` is called. Payloads loaded from an
existing packet stay as encoded bytes until first typed access, then the decoded
native object is cached for later reads. `get<T>()` returns a read-only
`payload_ref<T>` handle to the cached native object.

## Generate The SDK

The generated C++ SDK requires C++20 and compiler-matched FlatBuffers headers.

Generate the C++ SDK from the repository root:

```bash
python3 tools/flowdata/gen.py generate \
  --name metapoc \
  --version 1.2.3 \
  --sdk cpp \
  --cmake \
  --meson \
  --flatc "$(which flatc)" \
  --schema-dir payloads
```

Add `--cpp-python-bridge` when a C++ host application embeds Python and needs a
temporary live Python wrapper around an existing C++ envelope:

```bash
python3 tools/flowdata/gen.py generate \
  --name metapoc \
  --version 1.2.3 \
  --sdk cpp \
  --cpp-python-bridge \
  --cmake \
  --meson \
  --flatc "$(which flatc)" \
  --schema-dir payloads
```

For a CMake consumer, include the generated module and link `metapoc::sdk`:

```cmake
include("path/to/generated/cpp/cmake/metapoc.cmake")
metapoc_enable_sdk()

add_executable(my_app src/main.cpp)
target_link_libraries(my_app PRIVATE metapoc::sdk)
```

If `--cpp-python-bridge` was generated, create the consumer target first and
then attach the bridge source/link dependencies:

```cmake
include("path/to/generated/cpp/cmake/metapoc.cmake")

add_executable(my_host src/main.cpp)
metapoc_enable_python_bridge(my_host)
```

For a Meson consumer, include the generated integration directory and link
`metapoc_dep`:

```meson
subdir('generated/cpp/meson/metapoc')

executable(
  'my_app',
  'src/main.cpp',
  dependencies: [metapoc_dep],
)
```

The repository demo builds `demo/cpp/client.cpp` as `client` with both CMake and
Meson.

When the optional bridge was generated, include its Meson integration only for
embedding targets:

```meson
subdir('generated/cpp/meson/metapoc')
subdir('generated/cpp/meson/metapoc/python_bridge')

executable(
  'my_host',
  'src/host.cpp',
  dependencies: [metapoc_python_bridge_dep],
)
```

## Important Generated Names

For known payloads, application code uses generated FlatBuffers object API
native types:

```cpp
demo::perception::PerceptionT
demo::telemetry::TelemetryT
```

Reusable nested demo types from `payloads/common.fbs` live under
`demo::common`, for example `demo::common::BoundingBoxT`,
`demo::common::GeoPointT`, and `demo::common::Vector3T`.

`*T` types are FlatBuffers object API types. They are owned native C++ objects,
so nested tables are usually represented with `std::unique_ptr` and vectors of
`std::unique_ptr`.

FlatBuffers also generates raw table/accessor types such as
`demo::perception::Perception`. The envelope implementation uses those
internally for verification and unpacking, but the envelope API does not return
them.

## Tutorial: Build A Packet

Create native payload objects, append them, and serialize the envelope:

```cpp
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "metapoc.h"

std::vector<std::uint8_t> produce_packet() {
    demo::perception::PerceptionT front{};
    front.sensor_id = "front_camera";
    front.frame_id = 4821;
    front.timestamp_ms = 1714231234123ULL;

    auto det = std::make_unique<demo::perception::DetectionT>();
    det->track_id = 42;
    det->class_id = demo::perception::ClassId::VEHICLE;
    det->confidence = 0.94f;
    det->bbox = std::make_unique<demo::common::BoundingBoxT>();
    det->bbox->x = 321.0f;
    det->bbox->y = 188.0f;
    det->bbox->w = 128.0f;
    det->bbox->h = 72.0f;
    front.detections.push_back(std::move(det));

    demo::perception::PerceptionT rear{};
    rear.sensor_id = "rear_camera";
    rear.frame_id = 4822;
    rear.timestamp_ms = 1714231234124ULL;

    demo::telemetry::TelemetryT telemetry{};
    telemetry.vehicle_id = "demo-vehicle-01";
    telemetry.sequence = 9012;
    telemetry.timestamp_ms = 1714231234125ULL;

    metapoc::container::envelope envelope;
    envelope.add(front);
    envelope.add(rear);
    envelope.add(telemetry);

    auto packet = envelope.serialize();
    return {packet.data(), packet.data() + packet.size()};
}
```

The two `PerceptionT` values produce two entries with the same known payload
type. This is expected; known payload types are not unique keys. The known
payloads are not packed into FlatBuffers when `add()` is called; they are packed when the
envelope itself is serialized.

## Tutorial: Read A Packet

Construct an envelope from bytes, validate it, and read payloads by type and
occurrence index:

```cpp
#include <cstdint>
#include <span>
#include <vector>

#include "metapoc.h"

bool consume_packet(const std::vector<std::uint8_t>& bytes) {
    metapoc::container::envelope envelope(
        std::span<const std::uint8_t>(bytes.data(), bytes.size())
    );

    if (!envelope.valid()) {
        // Malformed outer envelope.
        const auto& reason = envelope.error();
        return false;
    }

    if (envelope.count<demo::perception::PerceptionT>() != 2) {
        return false;
    }

    auto front = envelope.get<demo::perception::PerceptionT>(0);
    auto rear = envelope.get<demo::perception::PerceptionT>(1);
    auto telemetry = envelope.get<demo::telemetry::TelemetryT>(0);

    if (!front || !rear || !telemetry) {
        return false;
    }

    const auto& front_payload = front->value();
    const auto& rear_payload = rear->value();
    const auto& telemetry_payload = telemetry->value();
    (void)front_payload;
    (void)rear_payload;
    (void)telemetry_payload;

    return envelope.contains<demo::telemetry::TelemetryT>();
}
```

`get<T>(index)` returns `std::optional<metapoc::container::payload_ref<T>>`.
The handle provides read-only access to the stored native object through
`value()`, `operator*()`, `operator->()`, and `get()`. For bytes loaded from a
packet, the first successful typed access decodes and caches that payload inside
the envelope; later reads of the same entry return handles to the cached native
object instead of unpacking the FlatBuffers bytes again.

## Thread Safety And Lifetime

The C++ envelope synchronizes payload storage with an internal mutex. Concurrent
`empty()`, `size()`, `count()`, `contains()`, `get()`, `add()`, and
`serialize()` calls on the same envelope instance are serialized internally,
including the first lazy decode/cache write. Returned `payload_ref<T>` handles
keep the referenced payload entry alive even if later `add()` calls reallocate
the envelope's internal entry vector.

The mutex protects envelope operations; `payload_ref<T>` protects the lifetime
of an already acquired payload entry. Caller code must still guarantee that the
envelope object outlives every thread or async task calling methods on it. Do
not destroy an envelope, move-construct from it, or copy/move assign over an
envelope while another thread may still be calling methods on the affected
object. Move construction and move assignment are `noexcept`; moved-from
envelopes remain valid but have unspecified contents.

## Iterate Matching Payloads

Use `for_each` when you want all entries with the same known payload type:

```cpp
std::size_t cameras = 0;

envelope.for_each<demo::perception::PerceptionT>(
    [&](const demo::perception::PerceptionT& perception) {
        ++cameras;
        // Use perception.sensor_id, perception.frame_id, ...
    }
);
```

`for_each()` builds a snapshot of matching payload references under the envelope
mutex, releases the mutex, and then invokes callbacks with `const T&`. The
callback does not receive a copy. Nested envelope reads inside the callback are
allowed. The callback reference is valid only for that callback invocation unless
you copy data out of it.

## Optional Embedded Python Bridge

`--cpp-python-bridge` generates:

```text
generated/cpp/python_bridge/metapoc_python_bridge.h
generated/cpp/python_bridge/metapoc_python_bridge.cpp
```

The bridge is for C++ hosts that embed CPython. It exposes a temporary Python
object wrapping an existing `metapoc::container::envelope&`; it does not replace
the generated Python packet SDK and it does not serialize/deserialize the whole
envelope for live access. The guest-script API is documented in
[C++ Python Bridge Guest Script API](cpp-python-bridge.md).

Building the bridge requires CPython 3.10 or newer development headers and
embedding library.

Host-side shape:

```cpp
#include <Python.h>

#include "metapoc.h"
#include "python_bridge/metapoc_python_bridge.h"

int run_python(metapoc::container::envelope& envelope) {
    metapoc::python_bridge::append_inittab();
    Py_Initialize();

    {
        metapoc::python_bridge::scoped_envelope live(envelope);
        PyObject* main = PyImport_AddModule("__main__");
        PyObject* globals = PyModule_GetDict(main);
        PyDict_SetItemString(globals, "env", live.py_object());
        PyRun_SimpleString("assert env.valid()");
    } // live wrapper is invalidated here

    Py_Finalize();
    return 0;
}
```

If the interpreter is already running, hold the GIL and call
`metapoc::python_bridge::initialize_module()` instead of `append_inittab()` and
`Py_Initialize()`. This creates the bridge dynamically and registers it in
`sys.modules` without taking interpreter ownership. The generated Python SDK
and its FlatBuffers dependency must be importable before this call.

The host must hold the Python GIL when constructing `scoped_envelope` and when
exposing or using its Python object.

Python callback code uses Python-style methods:

```python
from metapoc.fb.demo.perception.Perception import PerceptionT

assert env.count(PerceptionT) >= 0
payload = env.get(PerceptionT)
if payload is not None:
    print(payload.sensorId)

for payload in env.for_each(PerceptionT):
    print(payload.frameId)

from metapoc import external_key

payload = PerceptionT()
payload.sensorId = "python"
payload.frameId = 1
env.add(payload)
env.add(external_key("com.example.bridge.demo"), b"bytes")
```

Known `get()` and `for_each()` return read-only live proxy objects backed by C++
`payload_ref<T>` anchors. Nested tables and vectors are exposed as read-only
proxies/sequences. Scalar and string fields are returned as normal Python
values. Assigning to proxy fields raises `AttributeError`.

Known `add()` accepts generated Python FlatBuffers object API `*T` values. The
bridge packs that single Python object, verifies it against the expected C++
root type, unpacks it into a C++ native object, and appends it to the live
envelope. External Python bridge operations use the same generic method names
with generated `ExternalKey` handles; bare strings and integers are rejected.

The C++ host owns lifetime. The envelope must outlive the live wrapper, and the
host should invalidate the wrapper by ending the `scoped_envelope` before
returning from the callback boundary. Proxies already returned from `get()` can
outlive wrapper invalidation because they hold payload references. On the
invalidated live wrapper, `valid()` returns `False`; other envelope operations
raise `RuntimeError`.

The repository includes a checked-in bridge demo host and default Python script:

```text
demo/cpp/python_bridge_client.cpp
demo/python_bridge/live_demo.py
```

After generating both the C++ bridge and Python SDK, build the CMake demo and
run the bridge client:

```bash
python3 tools/flowdata/gen.py generate --name metapoc --version 1.2.3 --sdk cpp --cpp-python-bridge --cmake --schema-dir payloads
python3 tools/flowdata/gen.py generate --name metapoc --version 1.2.3 --sdk python --schema-dir payloads
cmake -S demo/cpp/cmake -B demo/cpp/cmake/build -DMETAPOC_PYTHON_BRIDGE_EXTRA_PATH=/path/to/site-packages
cmake --build demo/cpp/cmake/build
./demo/cpp/cmake/build/python_bridge_client
```

Pass another script path to execute custom Python against the local C++
envelope:

```bash
./demo/cpp/cmake/build/python_bridge_client path/to/script.py
```

The default script receives `env` in `__main__`, reads the C++ seed payload,
appends a known `PerceptionT`, appends an external payload, and verifies that
existing known payload proxies are read-only. If the active embedded Python
environment already imports `flatbuffers`, the extra CMake path is not needed.

## External And Unknown Payloads

External payloads are caller-owned opaque byte blobs. Use a stable namespaced
string key to create an `external_key_t` handle with `external_key()`. The SDK
stores the handle as a high-bit `uint64` id internally, but public external APIs
do not accept raw numeric ids:

```cpp
std::vector<std::uint8_t> blob{1, 2, 3};
const auto tracks_key =
    metapoc::container::external_key("com.example.tracker.tracks");
envelope.add(tracks_key, std::move(blob));

if (auto payload = envelope.get(tracks_key)) {
    auto bytes = payload->bytes();
}

std::array<std::uint8_t, 2> borrowed{4, 5};
envelope.add(tracks_key, std::span<const std::uint8_t>(borrowed));
```

Unknown, changed, or malformed same-id known-domain entries are kept as private
preserve-only entries and serialized again if the envelope is forwarded. Typed
`count<T>()`, `contains<T>()`, `get<T>()`, and `for_each<T>()` only include
entries that successfully verify and decode as `T`.

## Envelope API Reference

```cpp
metapoc::container::envelope envelope;
metapoc::container::envelope envelope(std::span<const std::uint8_t> packet);
```

State:

- `valid() -> bool`: true if the envelope is usable.
- `error() -> const std::string&`: validation error text, empty when valid.
- `producer_sdk_name() -> std::string`: SDK name recorded by the serializer.
- `producer_sdk_version() -> std::string`: SDK semantic version recorded by the
  serializer.
- `producer_schema_set_sha256() -> std::string`: schema-set digest recorded by
  the serializer.
- `producer_identity() -> producer_identity_status`: compare recorded producer
  metadata with the current generated SDK. Parsing is permissive for mismatches.
- `empty() -> bool`: true when there are no payload entries.
- `size() -> std::size_t`: number of payload entries.
- `reserve(payload_count)`: preallocate the internal payload entry list.

Writing:

- `add(native_payload)`: append a generated FlatBuffers object API `*T` value.
  The value is stored natively until serialization. Passing an lvalue copies the
  payload; passing an rvalue moves it into the envelope.
- `add(external_key_t, std::vector<std::uint8_t>)`: append caller-owned opaque
  bytes. Passing an lvalue vector copies the bytes; passing an rvalue vector
  moves the vector buffer into the envelope.
- `add(external_key_t, std::span<const std::uint8_t>)`: append borrowed opaque
  bytes by copying them into envelope-owned storage.
- `serialize() -> flatbuffers::DetachedBuffer`: serialize the envelope and stamp
  the current SDK name, version, and schema-set SHA-256.

Reading:

- `count<T>() -> std::size_t`: number of decodable entries with known payload type `T`.
- `contains<T>() -> bool`: true when at least one decodable matching known payload exists.
- `get<T>(index) -> std::optional<payload_ref<T>>`: return a read-only lifetime
  handle for the nth matching native entry.
- `for_each<T>(fn)`: call `fn(const T&)` for each matching entry without copying.

External payloads:

- `external_key(string_view) -> external_key_t`: precompute an external key handle.
- `is_external_key(key) -> bool`: true for handles created by `external_key()`.
- `count(key)`, `contains(key)`, `get(key, index)`, `for_each(key, fn)`: read
  external opaque byte payloads by precomputed `external_key_t` handle.
- `get(external_key_t, ...)` returns `std::optional<external_payload_ref>`.
  `external_payload_ref::bytes()` returns `std::span<const std::uint8_t>` and
  `external_payload_ref::key()` returns the associated key handle.

The C++ public API intentionally does not expose raw mutable access, erase,
edit, or upsert. If an envelope was constructed from invalid bytes, write and
serialize calls throw `std::logic_error`.

## Full Demo

In a full repository checkout, see `demo/cpp/client.cpp` for a complete threaded
producer/consumer example. It exercises every public envelope method and checks
typed payload access. Demos are not included in the generator source package.
