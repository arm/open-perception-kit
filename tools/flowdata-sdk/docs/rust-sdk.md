<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

# Rust SDK Guide

The Rust target generates an owning, standard-library crate in
`generated/rust`. Its Cargo package name and semantic version come from the
generator's `--name` and `--version` arguments, and its FlatBuffers dependency
is pinned to the exact compiler version used for generation.

Rust SDK generation also requires `rustfmt` on `PATH`.

```bash
python3 tools/flowdata/gen.py generate \
  --name metapoc \
  --version 1.2.3 \
  --sdk rust \
  --schema-dir payloads

cargo test --manifest-path generated/rust/Cargo.toml
```

## Generated Types

Owned FlatBuffers object types are exported below `fb`, preserving their schema
namespace. For example:

```rust
use metapoc::fb::demo::perception::PerceptionT;
```

Schema identifiers that are Rust keywords are escaped consistently with
`flatc`. Generator runtime internals are private and cannot collide with user
schema namespace roots.

## Envelope API

Known and external payloads share the same selector-based read methods:

```rust
use metapoc::fb::demo::perception::PerceptionT;
use metapoc::{external_key, external_payload, payload, Envelope};

let perceptions = payload::<PerceptionT>();
let diagnostics = external_key("com.example.diagnostics");

let mut envelope = Envelope::new();
envelope.add(PerceptionT::default());
envelope.add(external_payload(diagnostics, b"ready".to_vec()));

assert_eq!(envelope.count(perceptions), 1);
assert!(envelope.contains(perceptions));
assert!(envelope.get(perceptions, 0).is_some());
assert_eq!(envelope.get(diagnostics, 0), Some(b"ready".as_slice()));

for value in envelope.for_each(perceptions) {
    println!("frame {}", value.frame_id);
}
```

`payload::<T>()` creates a zero-sized selector for a generated known payload
type. `external_key()` creates a deterministic external selector, and
`external_payload()` combines that selector with bytes for `Envelope::add()`.
Bare numeric IDs cannot be used to read or append envelope entries.

## Ownership And Errors

`Envelope::decode(Vec<u8>)` owns its input. Invalid outer envelopes return
`EnvelopeDecodeError`, which retains the original bytes through `bytes()` and
`into_bytes()`. Known payloads with invalid identifiers or malformed
FlatBuffers remain preserved but are excluded from typed reads.

`Envelope::entries()` provides read-only diagnostics:

- known entries expose only their qualified type name
- external entries expose their `ExternalKey` and bytes
- unknown known-domain entries expose their numeric ID and bytes
- malformed known entries additionally expose the expected type and typed error

Preserved entries survive serialization unchanged. Serializing any valid
envelope stamps the current generated SDK name, version, and schema-set digest,
matching the C++, Python, and TypeScript SDK behavior.

## Concurrency

The crate has no async-runtime dependency. Owned envelopes, generated payloads,
selectors, and decode errors are `Send + Sync + 'static`, so applications may
move them between executor tasks. Encoding and decoding remain synchronous
in-memory operations; applications choose their own scheduling policy for
large packets.
