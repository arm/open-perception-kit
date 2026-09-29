/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.

use open_perception_kit::fb::open_perception_kit::metadata::BoxDetectionsT;
use open_perception_kit::{
    external_key, payload, EntryRef, Envelope, ProducerIdentityStatus, OPEN_PERCEPTION_KIT_NAME,
    OPEN_PERCEPTION_KIT_VERSION, SCHEMA_SET_SHA256,
};

const FIXTURE_SDK_NAME: &str = "perception";
const FIXTURE_SDK_VERSION: &str = "0.2.1";
const FIXTURE_SCHEMA_SET_SHA256: &str =
    "0ba6dfe959e1453ce12c7a8707623bc15d94d52c9235c26f7e27f31dda0775c5";

fn decode_hex(value: &str) -> Vec<u8> {
    let compact = value.trim();
    (0..compact.len())
        .step_by(2)
        .map(|index| u8::from_str_radix(&compact[index..index + 2], 16).unwrap())
        .collect()
}

fn assert_send_sync_static<T: Send + Sync + 'static>() {}
fn assert_send<T: Send>(_: T) {}

#[test]
fn decodes_python_produced_box_detections_packet() {
    assert_send_sync_static::<Envelope>();
    assert_send_sync_static::<BoxDetectionsT>();
    let packet = decode_hex(include_str!("fixtures/opk-box-detections-v0.2.1.hex"));
    let envelope = Envelope::decode(packet).expect("pinned OPK packet must decode");

    assert_eq!(envelope.producer_sdk_name(), FIXTURE_SDK_NAME);
    assert_eq!(envelope.producer_sdk_version(), FIXTURE_SDK_VERSION);
    assert_eq!(
        envelope.producer_schema_set_sha256(),
        FIXTURE_SCHEMA_SET_SHA256
    );
    let expected_identity = if OPEN_PERCEPTION_KIT_NAME != FIXTURE_SDK_NAME {
        ProducerIdentityStatus::SdkNameMismatch
    } else if OPEN_PERCEPTION_KIT_VERSION != FIXTURE_SDK_VERSION {
        ProducerIdentityStatus::SdkVersionMismatch
    } else if SCHEMA_SET_SHA256 != FIXTURE_SCHEMA_SET_SHA256 {
        ProducerIdentityStatus::SchemaSetMismatch
    } else {
        ProducerIdentityStatus::ExactMatch
    };
    assert_eq!(envelope.producer_identity(), expected_identity);
    assert_eq!(envelope.len(), 2);

    let box_detections = payload::<BoxDetectionsT>();
    let preserved_payload = if expected_identity == ProducerIdentityStatus::ExactMatch {
        let boxes = envelope
            .get(box_detections, 0)
            .expect("BoxDetections payload");
        let layer = boxes.layer.as_ref().expect("layer");
        assert_eq!(layer.engine.as_deref(), Some("fixture"));
        assert_eq!(layer.model.as_deref(), Some("yolov11n"));
        assert_eq!(layer.infer_element_id.as_deref(), Some("infer0"));
        let detections = boxes.detections.as_ref().expect("detections");
        assert_eq!(detections.len(), 1);
        let detection = &detections[0];
        assert_eq!(detection.class_id, 3);
        assert_eq!(detection.text.as_deref(), Some("car"));
        assert_eq!(detection.confidence, 0.875);
        let object = detection.object.as_ref().expect("object");
        assert_eq!(
            (object.id, object.parent_id, object.creation_ts_ns),
            (42, 7, 123_456_789)
        );
        let rectangle = detection.box_.as_ref().expect("box");
        assert_eq!(
            (rectangle.x, rectangle.y, rectangle.width, rectangle.height),
            (10.5, 20.25, 30.75, 40.5)
        );
        None
    } else {
        assert!(envelope.get(box_detections, 0).is_none());
        match envelope.entries().next() {
            Some(EntryRef::Unknown { id, bytes }) => Some((id, bytes.to_vec())),
            entry => panic!("expected preserved historical payload, got {entry:?}"),
        }
    };

    let key = external_key("com.arm.opk.fixture");
    assert_eq!(envelope.get(key, 0), Some(b"fixture-external".as_slice()));
    let roundtrip = Envelope::decode(envelope.serialize()).expect("round trip");
    if let Some((historical_id, historical_bytes)) = preserved_payload {
        assert!(
            matches!(roundtrip.entries().next(), Some(EntryRef::Unknown { id, bytes })
            if id == historical_id && bytes == historical_bytes)
        );
    } else {
        assert!(roundtrip.get(box_detections, 0).is_some());
    }
    assert_eq!(roundtrip.get(key, 0), Some(b"fixture-external".as_slice()));

    let future = async move { roundtrip.len() };
    assert_send(future);
}
