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

use open_perception_kit::fb::open_perception_kit::metadata::{
    BoxPromptInputsT, BoxPromptPayloadT, PromptAssociationT, PromptBoxT, RequestDestinationT,
    RequestEnvelopeT,
};
use open_perception_kit::{payload, Envelope};

#[test]
fn preserves_optional_payload_and_unsigned_ids() {
    let request_id = 0xfedcba9876543210_u64;
    let revision = 0xfffffffffffffffe_u64;
    let mut destination = RequestDestinationT::default();
    destination.inference_instance = Some("sam".into());
    destination.source_name = Some("pipelineVideoFrame".into());
    let mut request = RequestEnvelopeT::default();
    request.request_id = request_id;
    request.revision = revision;
    request.destination = Some(Box::new(destination));
    let mut box_geometry = PromptBoxT::default();
    box_geometry.top = 0.25;
    box_geometry.right = 1.0;
    box_geometry.bottom = 0.75;
    let mut box_payload = BoxPromptPayloadT::default();
    box_payload.box_ = Some(Box::new(box_geometry));
    let mut prompted = BoxPromptInputsT::default();
    prompted.request = Some(Box::new(request.clone()));
    prompted.payload = Some(Box::new(box_payload));
    let mut association = PromptAssociationT::default();
    association.request = Some(Box::new(request));
    association.session_id = Some("session-a".into());
    association.parent_id = 0x8000000000000001;
    let mut envelope = Envelope::new();
    envelope.add(prompted);
    envelope.add(BoxPromptInputsT::default());
    let mut absent_with_request = BoxPromptInputsT::default();
    absent_with_request.request = association.request.clone();
    envelope.add(absent_with_request);
    envelope.add(association);
    let result = Envelope::decode(envelope.serialize()).expect("round trip");
    let decoded = result
        .get(payload::<BoxPromptInputsT>(), 0)
        .expect("box prompt");
    assert_eq!(decoded.request.as_ref().unwrap().request_id, request_id);
    assert_eq!(decoded.request.as_ref().unwrap().revision, revision);
    assert_eq!(
        decoded.payload.as_ref().unwrap().box_.as_ref().unwrap().top,
        0.25
    );
    let absent = result
        .get(payload::<BoxPromptInputsT>(), 1)
        .expect("absent payload");
    assert!(absent.request.is_none());
    assert!(absent.payload.is_none());
    let absent_with_identity = result.get(payload::<BoxPromptInputsT>(), 2).unwrap();
    assert_eq!(
        absent_with_identity.request.as_ref().unwrap().request_id,
        request_id
    );
    assert!(absent_with_identity.payload.is_none());
    let associated = result
        .get(payload::<PromptAssociationT>(), 0)
        .expect("association");
    assert_eq!(associated.request.as_ref().unwrap().request_id, request_id);
    assert_eq!(associated.session_id.as_deref(), Some("session-a"));
    assert_eq!(associated.parent_id, 0x8000000000000001);
}
