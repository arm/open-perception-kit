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

# Open Perception Kit for Rust

`open_perception_kit` encodes and decodes OPK FrameResults packets with typed
payloads for detections, classifications, segmentation, tracking, and other
inference results. It owns decoded data and exposes native payload types under
`fb::open_perception_kit::metadata`.

## Install

Install from [crates.io](https://crates.io/crates/open_perception_kit):

```bash
cargo add open_perception_kit
```

Use a release with the same SDK major as the producer. Pin your tested version
and commit the application's `Cargo.lock` for reproducible builds. The crate
pins its FlatBuffers runtime; a separate FlatBuffers compiler is not required.
For snapshots and offline builds, follow the [bundle installation guide](https://github.com/arm/open-perception-kit/blob/main/docs/public/how-to/use-perception-sdk.md#integrate-the-rust-sdk).

## Example

Replace `src/main.rs` with this program and run `cargo run`. It creates a person
detection in a 640 by 480 image, serializes it, then decodes and selects the
result by producer ID. In a consumer, pass received bytes to `Envelope::decode`.

```rust
use open_perception_kit::fb::open_perception_kit::metadata::{
    BoundingBoxT, BoxDetectionT, BoxDetectionsT, CoordinateUnits, ImageSpaceT, LayerInfoT,
    ProducerInfoT,
};
use open_perception_kit::{payload, Envelope, ProducerIdentityStatus};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    const DETECTOR_ID: &str = "detector0";

    let mut space = ImageSpaceT::default();
    space.frame_id = 7;
    space.width = 640;
    space.height = 480;
    space.extent_width = 640.0;
    space.extent_height = 480.0;

    let mut rectangle = BoundingBoxT::default();
    rectangle.x = 10.0;
    rectangle.y = 20.0;
    rectangle.width = 80.0;
    rectangle.height = 120.0;
    rectangle.space = Some(Box::new(space));
    rectangle.units = CoordinateUnits::Pixels;

    let mut detection = BoxDetectionT::default();
    detection.box_ = Some(Box::new(rectangle));
    detection.class_id = Some(0);
    detection.confidence = 0.95;
    detection.text = Some("person".to_owned());

    let mut producer = ProducerInfoT::default();
    producer.instance_id = Some(DETECTOR_ID.to_owned());
    let mut layer = LayerInfoT::default();
    layer.producer = Some(Box::new(producer));
    let mut boxes = BoxDetectionsT::default();
    boxes.layer = Some(Box::new(layer));
    boxes.detections = Some(vec![detection]);

    let mut outgoing = Envelope::new();
    outgoing.add(boxes);
    let incoming = Envelope::decode(outgoing.serialize())?;
    if !matches!(
        incoming.producer_identity(),
        ProducerIdentityStatus::ExactMatch | ProducerIdentityStatus::CompatibleMatch
    ) {
        return Err("incompatible packet producer".into());
    }

    let selector = payload::<BoxDetectionsT>();
    let boxes = incoming
        .for_each(selector)
        .find(|boxes| {
            boxes
                .layer
                .as_ref()
                .and_then(|layer| layer.producer.as_ref())
                .and_then(|producer| producer.instance_id.as_deref())
                == Some(DETECTOR_ID)
        })
        .ok_or("missing detector0 results")?;
    let detections = boxes.detections.as_deref().unwrap_or_default();
    assert_eq!(detections.len(), 1);
    assert_eq!(detections[0].class_id, Some(0));
    println!("{} detection(s)", detections.len());
    Ok(())
}
```

The program prints `1 detection(s)`. Generated tables are non-exhaustive:
construct them with `Default::default()` and assign the fields you need.

## Reading results

Use `payload::<T>()` with `count`, `contains`, `get`, or `for_each`.
`count` counts payloads; a payload's collection contains the individual results.
Select pipeline outputs by payload type and `LayerInfo.producer.instance_id`.
Handle absent or repeated matches explicitly rather than relying on packet order.

An empty collection is a valid result; a missing payload means unavailable.
Optional tables, vectors, and measurements can be absent. For category IDs,
`None` means absent and `Some(0)` is a valid category.

Boxes use top-left coordinates and size in their `ImageSpace`. `Pixels` are
crop-local; `Normalized` values are fractions of the crop extent. The space
records source dimensions and the crop origin and extent. The example uses a
full-frame space with the default zero origin. See the [geometry contract](https://github.com/arm/open-perception-kit/blob/main/docs/arch/perception.md#image-geometry)
for mapping results to another image or display.

## Compatibility and errors

Require successful decode and an `ExactMatch` or `CompatibleMatch` before typed
access. Both require the expected SDK name; compatible versions share the same
SemVer major, including zero. Schema digests are diagnostic. SDK 1.0 requires
coordinated upgrades from older SDKs.

`EnvelopeDecodeError` retains invalid input through `bytes()` and `into_bytes()`.
Inspect unknown or malformed entries with `entries()` and `EntryRef`; their
bytes survive serialization. Unknown fields inside recognized payloads may be
lost during repacking, and `serialize()` stamps this SDK's producer identity.
Forward the original packet bytes when exact preservation is required.
Use `external_key` and `external_payload` for application-owned opaque bytes.

## Documentation

- [Rust API](https://docs.rs/open_perception_kit)
- [Installation and offline use](https://github.com/arm/open-perception-kit/blob/main/docs/public/how-to/use-perception-sdk.md#integrate-the-rust-sdk)
- [Payload contracts](https://github.com/arm/open-perception-kit/blob/main/docs/arch/perception.md)
- [OPK documentation](https://developer.arm.com/dev-docs/open-perception-kit/)
- [Source and issues](https://github.com/arm/open-perception-kit)

Licensed under [Apache-2.0](https://www.apache.org/licenses/LICENSE-2.0).
The crate includes `LICENSE` and `NOTICE`.
