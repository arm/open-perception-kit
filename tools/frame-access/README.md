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

# Native frame-access contract groundwork

This is the separate native transport schema workflow, not a new FlowData
metadata payload. `schemas/frame-access/frame_reply.fbs` is canonical. Its
namespace, absent file identifier and fields must remain unchanged without
explicit approval. Existing perception metadata validation is unaffected.

Generation uses the compiler version owned by `tools/perception/sdk.json`
(currently 25.9.23). It writes C++, Rust and binary-schema files to an explicit
build directory; consumers must use the same outputs, not maintain a copied
schema:

```sh
python3 tools/frame-access/generate.py --flatc /path/to/flatc --output /path/to/generated
c++ -std=c++17 -Wall -Wextra -Werror \
  -I/path/to/generated -I/path/to/flatbuffers/include \
  tools/frame-access/profile_test.cpp -o /path/to/profile_test
/path/to/profile_test
```

For the reproducible CTest target (including generated outputs):

```sh
cmake -S tools/frame-access -B /path/to/build \
  -DFLATC=/path/to/flatc -DFLATBUFFERS_INCLUDE_DIR=/path/to/flatbuffers/include
cmake --build /path/to/build
ctest --test-dir /path/to/build --output-on-failure
```

Keep OPK's existing compiler/C++/Python/TypeScript versions. Build iceoryx2
0.10.0 C++ with `IOX2_FEATURE_FLATBUFFERS=ON` and
`IOX2_DEPENDENCY_USE_SYSTEM_FLATBUFFERS=ON`, resolving the descriptor-pinned
FlatBuffers installation through `CMAKE_PREFIX_PATH`. The Rust native client
and OPK Rust SDK use FlatBuffers 25.12.19, as required by iceoryx2; the
25.9.23-generated accessors work with that runtime. The Rust artifact pin is
owned separately by `flatbuffers.rust_crates[0]` in `tools/perception/sdk.json`.
Cargo cannot combine the former exact 25.9.23 Rust pin with iceoryx2's
25.12.19 requirement. Serialized bytes cross
this boundary, not C++ runtime objects. No middleware patch or metadata SDK
compiler upgrade is needed for this combination. The OPK generator applies
the Rust-only runtime pin after verifying the original FlowData outputs, then
records it in the generated manifest; never edit generated Cargo files by hand.

## Native C++/Rust regression test

After building/installing unmodified iceoryx2 0.10.0 with the system-library
options above, enable the optional native test:

```sh
cmake -S tools/frame-access -B /path/to/build \
  -DNATIVE_TESTS=ON -DICEORYX2_SOURCE_DIR=/path/to/iceoryx2-0.10.0 \
  -DCMAKE_PREFIX_PATH=/path/to/native-install \
  -DFLATC=/path/to/flatc -DFLATBUFFERS_INCLUDE_DIR=/path/to/flatbuffers/include
cmake --build /path/to/build
ctest --test-dir /path/to/build --output-on-failure
```

The test uses the actual OPK C++ serializer and Rust decoder. It verifies six
synthetic packed formats, metadata-only and error replies, optional PTS, byte
identity, successful native growth and retained native samples after confirmed
producer exit. It runs static and growth cases sequentially in isolated
namespaces and terminates only its own child processes. Test capacity is two
clients/two requests with 1/1/1 response policy; only one client is exercised.
This is not GStreamer source mapping, multiclient fairness or Python ownership
coverage. Cargo builds in the CMake build directory and records its resolved
dependencies there. Point the source path at the same unmodified 0.10.0 tree
used to build the C++ binding.

## Explicit profile, no production defaults

`frame_access_profile.h` accepts client count, active requests per client,
returned-request budget per callback, and image/results/construction byte ceilings. All must be
explicit and positive. The calculation rejects invalid formats, zero dimensions,
oversized images/results, construction overruns and arithmetic overflow.
Negotiate dimensions before calling it. A caps change requires revalidation.
The optional `opkcomm` backend accepts these limits in `frame-profile`, creates
its endpoint after caps validation, and rejects caps growth beyond its initial
reservation. Cairn readiness/profile discovery is not yet integrated.

`callback_attempts` counts requests returned by native `receive()`, including
invalid requests rejected by OPK. Iceoryx2 discards already-canceled requests
internally; those do not count. This limits application request handling, not
middleware cleanup or total callback execution time under cancellation churn.

For width W and height H, let CW=(W+1)/2 and CH=(H+1)/2, evaluated in 64 bits.
Packed image byte lengths are:

| Format | Plane byte lengths |
| --- | --- |
| BGRA8 | 4WH |
| RGB8 | 3WH |
| Gray8 | WH |
| I420 | WH, CW×CH, CW×CH |
| NV12 | WH, 2CW×CH |
| YUY2 | 4CW×H |

The mapper must separately validate positive strides/readable spans and zero
the unused final Y1 byte of an odd-width YUY2 row. Size validation alone does
not validate source memory.

## Outer construction bound

Let I be the negotiated packed image bytes and R the explicit serialized
results ceiling. With ordinary generated table builders, direct byte-vector
construction, at most three planes, and C++ FlatBuffers 25.9.23:

- At most six tables (three Plane, Frame, SampleInfo, FrameReply). Budget
  80 bytes each: table offset, at most five fields each padded to eight bytes,
  vtable header/entries and alignment. This deliberately overcounts the
  smaller tables and does not rely on vtable deduplication.
- Five vectors at most: three plane byte vectors, results bytes and plane
  offsets. Budget 11 bytes each for length/alignment, plus 12 offset bytes.
- Root offset and final alignment: at most 11 bytes.
- Builder scratch: at most five 8-byte FieldLoc records plus six 4-byte saved
  vtable offsets = 64 bytes. Tables are built sequentially, not nested.

Success overhead is therefore at most 480+55+12+11+64 = 622 bytes.
An error instead has one table, a string of at most 512 bytes plus terminator,
length/alignment and root: even including 64 scratch bytes, less than 704 bytes.
Error fields and success fields must never be built together. Reserving
`align_up_8(I + R + 1024)` covers either path and the builder's 1024-byte initial
size without needing growth. Checked arithmetic keeps construction below the
signed 32-bit FlatBuffers limit.

The tests use a fixed allocator that throws if the builder attempts growth.
They cover all six formats at every width/height pair from 1 through 5,
VGA/1080p/4K and odd near-4K dimensions, maximum error-message length,
odd-plane sizes and invalid/overflow profiles. Image and construction ceilings
one byte below the calculated requirement must reject each tested layout.
Opaque result bytes in these construction tests are NOT valid FLWD fixtures.

This bounds only the outer loan-backed builder. It does not bound the existing
nested OPK serializer's allocations, result-model storage, source mapping,
process RSS, allocator metadata or committed OS backing. Serialized segmentation
bytes count against R, but R is not a universal model-memory bound.

For the native 1 queued / 1 borrowed / 1 writer-loan policy, with replacement
and fire-and-forget disabled, response-slot count is 6×clients×active requests.
`responsePayloadCapacity` is slots×construction bytes, not the complete shared
memory allocation or a backing guarantee. Request/control storage, slot headers,
alignment and deployment overhead must be budgeted separately.

## Test-only policy and remaining gates

The standalone profile tests use 2 clients, 2 active requests per client,
4 callback attempts, 1024 image bytes, 257 results bytes and 4096 construction
bytes. Representative tests separately use 32 MiB image, 64 MiB results and
97 MiB construction ceilings, with the same request counts. They construct one
reply at a time, not a native pool. These are test inputs only, not recommended
production values; the 64 MiB candidate does not establish serializer or backing
safety.

Production sizing, nested serializer bounds, backing admission and recovery
from native allocation/growth failures remain approval-gated. A static pool
is not reserved tmpfs backing; successful fixed-allocator tests do not prove
graceful memory exhaustion. No middleware changes are included here.
