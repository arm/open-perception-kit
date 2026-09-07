# Perception Rust Cross-Language Fixture

`opk-box-detections-v0.2.1.hex` is the hexadecimal encoding of a 496-byte
`FLWD` packet produced by the checked-in OPK Python SDK version `0.2.1` on
August 27, 2026. It contains one populated `BoxDetections` payload followed by
the external payload `com.arm.opk.fixture` with bytes `fixture-external`.
Its producer schema-set SHA-256 is
`0ba6dfe959e1453ce12c7a8707623bc15d94d52c9235c26f7e27f31dda0775c5`.
When the current schema changes that payload's generated ID, the Rust test
requires the historical payload to remain preserved as an unknown entry.

The fixture is AMP-owned test data. The generic FlowData generator does not
contain Perception schemas, values, or product-specific compatibility tests.
