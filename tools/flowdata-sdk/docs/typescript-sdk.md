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

# TypeScript SDK Guide

The TypeScript SDK is generated into `generated/ts`. It exposes an envelope API
for reading packets and writing new packets from generated native payload
objects.

```ts
import {
  Envelope,
  FLATBUFFERS_VERSION_REQUIREMENT,
  METAPOC_VERSION,
} from 'metapoc';
```

`METAPOC_VERSION` contains the stable semantic version supplied during
generation and matches the generated `package.json` version.
`FLATBUFFERS_VERSION_REQUIREMENT` is `>=24.3.25 <26.0.0`, matching the
generated package dependency.

Known payload ids are generated internally and are not part of the public API.
Known payload operations use the generated FlatBuffers object API native
classes, such as `PerceptionT` and `TelemetryT`.

## Generate And Build The SDK

```bash
python3 tools/flowdata/gen.py generate \
  --name metapoc \
  --version 1.2.3 \
  --sdk ts \
  --flatc "$(which flatc)" \
  --schema-dir payloads
```

```bash
cd generated/ts
npm install
npm run typecheck
npm run build
```

## Important Generated Names

```ts
import { Envelope } from 'metapoc';
import { BoundingBoxT } from 'metapoc/fb/demo/common/bounding-box.js';
import { GeoPointT } from 'metapoc/fb/demo/common/geo-point.js';
import { Vector3T } from 'metapoc/fb/demo/common/vector3.js';
import { PerceptionT } from 'metapoc/fb/demo/perception/perception.js';
import { TelemetryT } from 'metapoc/fb/demo/telemetry/telemetry.js';
```

The `*T` classes are generated FlatBuffers object API native classes. They
expose fields such as `sensorId`, `frameId`, `vehicleId`, and `temperaturesC`.

## Read A Packet

```ts
import { readFile } from 'node:fs/promises';
import { Envelope } from 'metapoc';
import { PerceptionT } from 'metapoc/fb/demo/perception/perception.js';
import { TelemetryT } from 'metapoc/fb/demo/telemetry/telemetry.js';

const packet = new Uint8Array(await readFile('packet.bin'));
const envelope = new Envelope(packet);

if (!envelope.valid()) {
  throw new Error(envelope.error() ?? 'invalid packet');
}

const front = envelope.get(PerceptionT, 0);
const rear = envelope.get(PerceptionT, 1);
const telemetry = envelope.get(TelemetryT, 0);

console.log(envelope.count(PerceptionT));
console.log(front?.sensorId, rear?.sensorId, telemetry?.vehicleId);
```

`get()` returns generated object API native values. The SDK does not expose raw
FlatBuffers table objects or known-payload blobs through the public API.

## Write A Packet

Create generated native payload objects, append them to an empty envelope, and
serialize:

```ts
import { Envelope } from 'metapoc';
import { PerceptionT } from 'metapoc/fb/demo/perception/perception.js';

const front = new PerceptionT(
    1,
    0,
    'front_camera',
    BigInt(4821),
    BigInt(1714231234123),
    [],
);

const envelope = new Envelope();
envelope.add(front);
const packet = envelope.serialize();
```

The single-argument `add()` overload accepts generated native payload objects only.

## External And Unknown Payloads

External payloads are caller-owned opaque bytes addressed by `ExternalKey`
handles. The SDK stores the handle as a high-bit `uint64` id internally, but
public envelope APIs do not accept bare strings or `bigint` values for external
payload access:

```ts
import { external_key } from 'metapoc';

const tracksKey = external_key('com.example.tracker.tracks');
envelope.add(tracksKey, bytes);
const payload = envelope.get(tracksKey);
```

TypeScript can also parse and reserialize envelopes that contain low-domain
payload ids not known by the generated SDK, or known ids whose blobs do not
verify as the expected type. Those entries are private preserve-only data and
remain in the packet when `serialize()` is called. Typed `count()`, `contains()`,
`get()`, and `for_each()` only include entries that successfully decode as the
requested type.

## API Reference

State:

- `valid() -> boolean`
- `error() -> string | null`
- `empty() -> boolean`
- `size() -> number`

Known payloads and external payloads:

- `add(nativeObject) -> void`: append a known generated object-api payload
- `add(externalKey, blob) -> void`: append external opaque bytes
- `count(payloadClass | externalKey) -> number`
- `contains(payloadClass | externalKey) -> boolean`
- `get<T>(payloadClass, index = 0) -> T | null`
- `get(externalKey, index = 0) -> Uint8Array | null`
- `for_each<T>(payloadClass) -> IterableIterator<T>`
- `for_each(externalKey) -> IterableIterator<Uint8Array>`

External payloads:

- `external_key(key: string) -> ExternalKey`
- `is_external_key(key: unknown) -> boolean`

Serialization:

- `serialize() -> Uint8Array`

Producer identity:

- `producerSdkName() -> string`
- `producerSdkVersion() -> string`
- `producerSchemaSetSha256() -> string`
- `producerIdentity() -> ProducerIdentityStatus`

Parsing does not reject a missing or mismatched identity. `serialize()` stamps
the current generated SDK identity, including when forwarding an envelope
produced by another SDK.

`new Envelope(packet)` checks the outer envelope file identifier and parses the
outer packet through generated FlatBuffers accessors. It does not provide
verifier-equivalent structural validation, so boundary code should still apply
practical size and trust limits. Payload compatibility is checked per payload id
and blob when typed APIs are called: entries that do not decode through the typed
API are preserved internally for roundtrip serialization. If an envelope is
invalid, `add()` and `serialize()` throw.

`get()` and `for_each()` return copied `Uint8Array` values for `ExternalKey`
selectors. The SDK does not expose mutable views into stored external blobs.
