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

# Integration Guide

This guide explains how to integrate the generated envelope SDK concept into
another internal project.

## Intended Use

Use this project when a pipeline or application needs to pass a heterogeneous set
of metadata payloads between components while keeping payload schemas strongly
typed for known internal modules.

The typical model is:

1. The consuming project owns a FlatBuffers schema set.
2. The consuming project's build or CI pipeline runs the SDK generator.
3. Its manifest is verified before the SDK is compiled or packaged with its consumers.
4. Runtime components pass serialized envelope bytes.
5. Components access known payloads through typed APIs.
6. Components can transport custom opaque bytes through external payload APIs
   using key handles returned by `external_key()` in every language.

Unknown or undecodable known-domain entries are preserved when
an envelope is parsed and serialized again. C++, Python, and TypeScript keep
them private; Rust additionally exposes read-only diagnostic views. External
payloads are explicit caller-owned key/blob entries and are never interpreted
by the SDK.

## Recommended Project Layout

A consuming project should keep the schema set in one directory:

```text
project/
  schemas/
    common.fbs
    perception.fbs
    telemetry.fbs
    osd.fbs
  generated/
  src/
  build scripts...
```

Support schemas define reusable nested types and do not declare `root_type`.
Payload schemas declare exactly one `root_type` and one four-character
`file_identifier`.

Example support schema:

```fbs
namespace app.common;

table Point {
  x:float;
  y:float;
}
```

Example payload schema:

```fbs
include "common.fbs";

namespace app.osd;

table Line {
  a:app.common.Point;
  b:app.common.Point;
}

table Overlay {
  lines:[Line];
}

root_type Overlay;
file_identifier "OSDD";
```

## Choose The SDK Name

The generator requires `--name <sdk_name>`.

The name controls:

- C++ namespace and public header name.
- Python package name.
- Rust crate name.
- TypeScript package name and import root.
- CMake target and Meson dependency names.

Use a short project-specific lowercase name:

```bash
--name project_sdk
```

The name must match `[a-z][a-z0-9_]*` and must not be a target-language
keyword or a Python runtime module name (including standard-library modules,
`flatbuffers`, and `numpy`). The schema namespaces do not need to contain the SDK name.

## Choose The Release Version

The consuming project that provides the schemas owns the generated SDK's version
and the decision to bump major, minor, or patch. flowdata-sdk owns the mechanics:
syntax validation, propagation into generated artifacts and envelope metadata,
and advisory comparison. The generator's own version is a separate release
identity owned by the flowdata-sdk repository.

Payload-level schema version markers are separate and optional. A consuming
project may deliberately omit them; flowdata-sdk does not require or interpret
such fields or perform schema-level compatibility decisions on the project's behalf.

Every `generate` invocation requires the same stable semantic version for one
multi-language SDK release:

```bash
--version 1.2.3
```

The generator accepts `MAJOR.MINOR.PATCH` without leading zeroes. It propagates
that value to package metadata and name-derived constants but does not choose or
validate the release bump. The SDK-producing project owns its compatibility
promise: use patch releases for compatible fixes, minor releases for compatible
additions, and major releases for breaking APIs, wire layouts, or semantics.
Payload ids remain stable across all SDK versions. Accepting newer producers
with older readers requires compatible wire evolution within the major, not
just backward-compatible source APIs.

For `--name project_sdk`, consumers can inspect `project_sdk::PROJECT_SDK_VERSION`
in C++ and Rust, `project_sdk.PROJECT_SDK_VERSION` in Python,
`PROJECT_SDK_VERSION` in TypeScript and CMake, and `project_sdk_version` in Meson.

## Generate SDKs

Run the generator from the consuming project's build or CI script.

C++:

```bash
python3 path/to/tools/flowdata/gen.py generate \
  --name project_sdk \
  --version 1.2.3 \
  --sdk cpp \
  --cmake \
  --meson \
  --schema-dir schemas \
  --generated-root generated
```

Python:

```bash
python3 path/to/tools/flowdata/gen.py generate \
  --name project_sdk \
  --version 1.2.3 \
  --sdk python \
  --schema-dir schemas \
  --generated-root generated
```

Rust (requires `rustfmt` on `PATH`):

```bash
python3 path/to/tools/flowdata/gen.py generate \
  --name project_sdk \
  --version 1.2.3 \
  --sdk rust \
  --schema-dir schemas \
  --generated-root generated
```

TypeScript:

```bash
python3 path/to/tools/flowdata/gen.py generate \
  --name project_sdk \
  --version 1.2.3 \
  --sdk ts \
  --schema-dir schemas \
  --generated-root generated
```

Use `--flatc /path/to/flatc` when the build environment needs an explicit
FlatBuffers compiler path.

Each generation replaces the selected language SDK root. Include every needed
integration/bridge flag on the C++ invocation. Verify the pristine manifests
before builds, imports or package tools add files under those roots.

## FlatBuffers Toolchain Contract

Use a `flatc` version satisfying `>=24.3.25,<26.0.0`. The generator checks this
range before producing output. Generated Python and TypeScript package metadata
declare the corresponding runtime range, so normal package installation rejects
unsupported runtimes.

Generated Rust crates pin the exact FlatBuffers runtime version used by the
compiler, matching the generated Rust source compatibility contract.

Generated C++ headers require the exact FlatBuffers header version used by the
compiler. This is stricter than the generator-supported range because upstream
FlatBuffers C++ output emits an exact-version compile-time check. Use the
generated `<SDK>_FLATBUFFERS_VERSION_REQUIREMENT` C++, CMake, or Meson value when
assembling the C++ dependency closure.

## C++ Build Integration

For CMake consumers, include the generated CMake module:

```cmake
include("${CMAKE_CURRENT_SOURCE_DIR}/generated/cpp/cmake/project_sdk.cmake")
project_sdk_enable_sdk()

target_link_libraries(my_element PRIVATE project_sdk::sdk)
```

For Meson consumers, add the generated subdir and dependency:

```meson
subdir('generated/cpp/meson/project_sdk')

executable(
  'my_element',
  'my_element.cpp',
  dependencies: [project_sdk_dep],
)
```

The core generated C++ SDK and the FlatBuffers runtime used here are header-only;
no FlatBuffers library is linked by these integrations. Consumers compile with
C++20. The optional bridge below adds a compiled source file and CPython linkage.

If the C++ host embeds Python and wants Python callbacks to inspect or append to
the live C++ envelope, generate the optional bridge:

```bash
python3 path/to/tools/flowdata/gen.py generate \
  --name project_sdk \
  --version 1.2.3 \
  --sdk cpp \
  --cpp-python-bridge \
  --cmake \
  --meson \
  --schema-dir schemas \
  --generated-root generated
```

The bridge adds generated files under `generated/cpp/python_bridge/` and is not
header-only. Generate the Python SDK too and make it and `flatbuffers` importable
by the embedded interpreter for `<sdk>.guest` scripts, including read-only ones.
CMake consumers attach it to a concrete target:

```cmake
include("${CMAKE_CURRENT_SOURCE_DIR}/generated/cpp/cmake/project_sdk.cmake")

add_library(my_python_host src/python_host.cpp)
project_sdk_enable_python_bridge(my_python_host)
```

Building the bridge requires CPython 3.10 or newer development headers and
embedding library. The generated Python SDK and supported Python FlatBuffers
runtime must be importable by the embedded interpreter, including for read-only
guest scripts.

The live bridge is scoped by the embedding host:

- The host owns the `project_sdk::container::envelope`.
- The host creates `python_bridge::scoped_envelope live(envelope)` while holding
  the Python GIL, keeps `live` alive throughout the callback, and exposes
  `live.py_object()`. Do not take the pointer from
  a temporary wrapper that is destroyed before the callback runs.
- Python known reads return read-only proxy objects backed by C++ payload
  references.
- Python known writes append new generated object-api payload values.
- Python external writes/read use the generic `add`, `get`, `count`,
  `contains`, and `for_each` methods with generated `ExternalKey` handles.
- Existing entries cannot be edited, erased, or mutated in place through the
  bridge.
- Endpoint clients import the packet-owning API from `<sdk>.packet`; embedded
  guest scripts import the host-backed API from `<sdk>.guest`.

This repository's runnable bridge example is
`demo/cpp/python_bridge_client.cpp` plus `demo/python_bridge/live_demo.py`. The
host creates a local C++ envelope, exposes it to the script as `env`, executes
the script, and then prints the resulting known/external payload counts. Pass a
different script path to reuse the same host with custom callback code.

## Runtime Pattern

Create an envelope, append known payloads, and serialize:

```cpp
#include "project_sdk.h"

std::vector<std::uint8_t> produce() {
    app::osd::OverlayT overlay{};

    project_sdk::container::envelope envelope;
    envelope.add(overlay);

    auto packet = envelope.serialize();
    return {packet.data(), packet.data() + packet.size()};
}
```

Read an envelope and fetch known payloads by type:

```cpp
bool consume(std::span<const std::uint8_t> packet) {
    project_sdk::container::envelope envelope(packet);
    if (!envelope.valid()) {
        return false;
    }

    using status = project_sdk::container::producer_identity_status;
    const auto match = envelope.producer_identity();
    if (match != status::exact_match && match != status::compatible_match) {
        return false;
    }

    auto overlay = envelope.get<app::osd::OverlayT>();
    if (!overlay) {
        return true;
    }

    const auto& overlay_payload = overlay->value();
    // Use overlay_payload.lines, etc.
    return true;
}
```

Use multiple entries of the same payload type when several components need to
append independent contributions:

```cpp
project_sdk::container::envelope envelope;
envelope.add(first_overlay);
envelope.add(second_overlay);

for (std::size_t i = 0; i < envelope.count<app::osd::OverlayT>(); ++i) {
    auto overlay = envelope.get<app::osd::OverlayT>(i);
    if (overlay) {
        draw(overlay->value());
    }
}
```

Avoid treating one payload as a global mutable object. Prefer append-only
payload contributions from each producer module. A downstream renderer or OSD
module can iterate all matching payloads and draw what it understands.

Transport external opaque bytes only when the producing and consuming modules
own a protocol outside the generated SDK:

```cpp
constexpr auto tracks_key =
    project_sdk::container::external_key("com.example.tracker.tracks");

project_sdk::container::envelope envelope;
std::vector<std::uint8_t> encoded_tracks = encode_custom_tracks();
envelope.add(tracks_key, std::move(encoded_tracks));

if (auto tracks = envelope.get(tracks_key)) {
    decode_custom_tracks(tracks->bytes());
}
```

The string key is hashed into a high-bit `uint64` id internally. The SDK stores
only that id and the blob; it does not store the original string and does not
decode or validate external blob contents.

## GStreamer Integration Pattern

For a GStreamer pipeline, keep the envelope bytes attached to the buffer as
metadata or side data owned by the pipeline.

Recommended element behavior:

- Source/preprocessor elements create an envelope if one is missing.
- Elements that produce metadata parse the current envelope, append their typed
  payload contribution, and serialize the envelope back to metadata storage.
- Elements that consume metadata parse the envelope and read the known payload
  types they understand.
- Elements should not erase, edit, or upsert payloads owned by other elements.
- OSD/rendering elements should draw based on payloads found in the envelope, not
  based on compile-time knowledge of producer modules.

For C++, make sure the `envelope` object outlives any thread or async task using
it. The object is internally synchronized, but lifetime is still the caller's
responsibility.

## Compatibility Model

Known payload ids hash only the case-sensitive, fully qualified FlatBuffers
root name (for example `app.osd.Overlay`). The algorithm uses SHA-256 of its UTF-8
bytes, the first eight digest bytes interpreted as an unsigned big-endian
integer, then `value % (2**63 - 1) + 1`. File identifiers, schema contents,
includes, paths, and SDK versions do not participate. Generation rejects id
collisions within the schema set. Do not reuse qualified names for unrelated
payload families.

Compatible table additions keep typed access working: an SDK 3.0.0 reader can
read its existing fields from a 3.1.0 producer; a 3.1.0 reader sees defaults for
new fields in a 3.0.0 packet. Append optional/defaulted fields without moving
existing field slots, and preserve existing defaults, meanings, and file
identifiers. Apply the same discipline to included support schemas. New payload
types unknown to an older reader remain preserve-only entries, with Rust diagnostic views.

An incompatible edit also retains its id if the qualified root name is unchanged.
Matching ids and successful decoding do not prove compatibility. The SDK publisher
owns schema/API evolution and version assignment; applications must enforce
acceptance policy before interpreting payloads.

Every serialized envelope also records the SDK name, SDK semantic version, and
schema-set SHA-256 of the serializer. The generated envelope API exposes these
values and a producer-identity status. Evaluate the name first, then the version:

| Condition | Status |
|---|---|
| Name missing, malformed, or different | `name_mismatch` |
| Matching name; version missing or malformed | `version_mismatch` |
| Matching name and full version | `exact_match` |
| Matching name; different versions sharing a major (including zero) | `compatible_match` |
| Otherwise | `version_mismatch` |

Before its first serialization, a newly constructed envelope reports the local
SDK's metadata. A packet-loaded envelope reports the metadata stored in that packet.

Versions use the stable `MAJOR.MINOR.PATCH` syntax of the generated SDK's
`--version` input.
The same-major rule applies uniformly, including `0.x.y`: for example, a `0.1.0`
reader classifies a `0.2.0` producer as compatible. This is the SDK's application
policy, not a guarantee from SemVer; publishers using it must preserve wire
compatibility within major zero too. Schema hashes are excluded from
classification, even when missing, malformed, or different.
An exact name/version match therefore does not imply identical schema builds;
compare `producer_schema_set_sha256` separately when needed.

Parsing and typed reads are not gated by this advisory result. An application
whose publisher promises same-major wire compatibility can accept both exact
and compatible matches, for example in Python:

```python
from metapoc.packet import Envelope, ProducerIdentityStatus

envelope = Envelope(packet)
if not envelope.valid():
    raise ValueError(envelope.error())
if envelope.producer_identity() not in (
    ProducerIdentityStatus.EXACT_MATCH,
    ProducerIdentityStatus.COMPATIBLE_MATCH,
):
    raise ValueError("unsupported producer SDK")
```

Applications can inspect the exposed producer version and local SDK version
constant to apply stricter directional rules. When comparing order, compare
numeric version components, not strings. Reserialized output records the SDK
performing that serialization; the live envelope's stored producer metadata is
unchanged. The output does not retain the first producer's identity.

### Migration From Content-Derived IDs

Generator 0.7.0 replaces the payload-id algorithm and the previous six-state
producer-identity enum. The 0.6.0 release uses the old content-derived scheme;
0.7.0 uses canonical root names and the four-state identity API.
Regenerate producer and reader SDKs together and update applications referencing
removed enum values. The wire envelope
layout is unchanged, but older generated SDKs cannot recognize the new ids
automatically; their payloads remain unknown entries rather than gaining typed
interoperability retroactively. Coordinate release/deployment of the new SDKs.

Treat adoption as a breaking release of a previously published consuming SDK:
bump its major version (including `0` to `1`) when relying on same-major acceptance,
and update application acceptance policies. The generator version is separate
from the required SDK `--version`; changing the generator does not bump the SDK
version automatically. Reusing the SDK name/version and unchanged schemas can
report `exact_match` with a matching schema digest while legacy payload ids remain
unrecognized. A generator upgrade alone does not make mixed legacy/new packets
compatible.

External payload key handles are deterministic hashes of user-provided string
keys with the high bit set. Known generated ids keep the high bit clear, so
known and external payload domains cannot collide. Public APIs accept generated
external key handles, not arbitrary strings or numeric ids.

## Validation And CI

At minimum, a consuming project should test:

- SDK generation from its schema directory.
- C++ build integration for every target that uses the SDK.
- A roundtrip envelope through representative pipeline elements.
- External payload roundtrips if custom opaque payload transport is used.
- Partial compatibility, if mixed-version components are expected.
- Maximum expected envelope size and payload count.

For this repository, run:

```bash
./tests/sanity_check.py --keep-generated
```

In a consuming project, create an equivalent integration test around the actual
schemas and build system.

Each generated language root contains `flowdata-manifest.json`. Release tooling
can consume it to verify the SDK name/version, generator and FlatBuffers compiler
versions, supported compiler range, selected runtime requirements, enabled
outputs, schema-set digest, payload identities, and every generated file hash
before packaging. Generated artifact paths are relative to the manifest's complete
SDK root; schema-file and payload `schema` paths are relative to `--schema-dir`.
Neither contains machine-specific absolute paths.

The current manifest has no independent format version. It also exposes
descriptors for downstream tooling. Python SDK
manifests identify the distribution/import names, Python requirement, build
backend, wheel tag, typing marker, and stubs. Bridge-enabled C++ manifests
identify the Python module, generated source/header, registration function,
wrapper type, SDK import name, and Python requirement. Consume these descriptors
instead of rebuilding generator naming rules in release or executor tooling.

Use the generator's verifier instead of reimplementing these checks:

```bash
python3 tools/flowdata/gen.py verify-manifest \
  generated/python/flowdata-manifest.json \
  --schema-root payloads
```

The manifest directory is the complete generated SDK root. The command
validates the current manifest shape and metadata as well as path safety,
file existence, byte size, and SHA-256 content hashes. A failed check exits with
a nonzero status and a specific diagnostic suitable for CI logs.
If the consuming project adds copyright headers, reformats files, or otherwise
decorates generated output, it must create final release hashes after that
postprocessing; the flowdata manifest intentionally records generator output.
The complete-root check also rejects unlisted build/install by-products such as
`dist/`, `node_modules/` and `__pycache__/`; verify before creating them.

## Operational Boundaries

This is intended for internal controlled inputs.

Before using it with untrusted inputs, add boundary checks for:

- Maximum envelope byte size.
- Maximum payload count.
- Maximum payload blob size.
- Maximum accepted schema/version combinations, if needed.

See [Known Limitations](../KNOWN_LIMITATIONS.md) for the current risk boundaries.
