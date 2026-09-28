<!-- SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates -->

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
3. The generated SDK is compiled or packaged with the modules that use it.
4. Runtime components pass serialized envelope bytes.
5. Components access known payloads through typed APIs.
6. Components can transport custom opaque bytes through external payload APIs
   using key handles returned by `external_key()`; bare strings and numeric ids
   are not accepted.

Unknown, changed, or malformed same-id known-domain entries are preserved when
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
keyword. The schema namespaces do not need to contain the SDK name.

## Choose The Release Version

Every `generate` invocation requires the same stable semantic version for one
multi-language SDK release:

```bash
--version 1.2.3
```

The generator accepts `MAJOR.MINOR.PATCH` without leading zeroes. It propagates
that value to package metadata and name-derived constants but does not choose or
validate the release bump. Use patch releases for implementation fixes with an
unchanged API and schema set, minor releases for compatible API additions or new
payload types, and major releases for breaking APIs or changes to existing
payload schemas that alter their generated payload identities.

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

The generated C++ SDK is header-only apart from its FlatBuffers dependency.
Consumers must compile with C++20.

If the C++ host embeds Python and wants Python callbacks to inspect or append to
the live C++ envelope, generate the optional bridge:

```bash
python3 path/to/tools/flowdata/gen.py generate \
  --name project_sdk \
  --version 1.2.3 \
  --sdk cpp \
  --cpp-python-bridge \
  --cmake \
  --schema-dir schemas \
  --generated-root generated
```

The bridge adds generated files under `generated/cpp/python_bridge/` and is not
header-only. CMake consumers attach it to a concrete target:

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
  `live.py_object()`.
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

Known payload ids are generated from:

- Qualified root type.
- Payload `file_identifier`.
- Root schema content and transitive include content.

If a payload schema or included support schema changes, the id for affected
payload roots changes. Consumers with an older SDK will no longer expose that
payload through typed APIs, but the entry is preserved when the
envelope is serialized again.

Unchanged payloads remain readable even when other payloads in the same envelope
changed.

Every serialized envelope also records the SDK name, SDK semantic version, and
schema-set SHA-256 of the serializer. The generated envelope API exposes these
values and a producer-identity status with these outcomes:

- `exact_match`
- `missing` for packets created before producer metadata was added
- `malformed`
- `sdk_name_mismatch`
- `sdk_version_mismatch`
- `schema_set_mismatch`

Parsing remains permissive for every status. Applications can require
`exact_match` when they need to confirm the precise SDK release and schema set,
or continue using the payloads that remain compatible. When an envelope is
serialized again, the output packet records the identity of the SDK performing
that serialization; the live envelope's stored producer metadata is unchanged.

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
before packaging. Generated-file paths are normalized POSIX paths relative to
the manifest's SDK root, including Meson outputs, and contain no machine-specific
absolute paths.

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

## Operational Boundaries

This is intended for internal controlled inputs.

Before using it with untrusted inputs, add boundary checks for:

- Maximum envelope byte size.
- Maximum payload count.
- Maximum payload blob size.
- Maximum accepted schema/version combinations, if needed.

See [Known Limitations](../KNOWN_LIMITATIONS.md) for the current risk boundaries.
