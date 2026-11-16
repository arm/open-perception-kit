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
import { ByteBuffer } from 'flatbuffers';
import { BoxDetections as BoxDetections_3248104387212218840, BoxDetectionsT as BoxDetectionsT_3248104387212218840 } from './fb/open-perception-kit/metadata/box-detections.js';
import { CameraContacts as CameraContacts_7630654510173219138, CameraContactsT as CameraContactsT_7630654510173219138 } from './fb/open-perception-kit/metadata/camera-contacts.js';
import { Classifications as Classifications_3375056508603945320, ClassificationsT as ClassificationsT_3375056508603945320 } from './fb/open-perception-kit/metadata/classifications.js';
import { FrameContext as FrameContext_5246681475592864076, FrameContextT as FrameContextT_5246681475592864076 } from './fb/open-perception-kit/metadata/frame-context.js';
import { InstanceSegmentationMasks as InstanceSegmentationMasks_7634499802271603959, InstanceSegmentationMasksT as InstanceSegmentationMasksT_7634499802271603959 } from './fb/open-perception-kit/metadata/instance-segmentation-masks.js';
import { ObjectEmbeddings as ObjectEmbeddings_6121378805478677067, ObjectEmbeddingsT as ObjectEmbeddingsT_6121378805478677067 } from './fb/open-perception-kit/metadata/object-embeddings.js';
import { ObjectTracks as ObjectTracks_5316059591418463553, ObjectTracksT as ObjectTracksT_5316059591418463553 } from './fb/open-perception-kit/metadata/object-tracks.js';
import { PerformanceMeasurements as PerformanceMeasurements_2630163151625760217, PerformanceMeasurementsT as PerformanceMeasurementsT_2630163151625760217 } from './fb/open-perception-kit/metadata/performance-measurements.js';
import { PersonPresences as PersonPresences_7550522492754457054, PersonPresencesT as PersonPresencesT_7550522492754457054 } from './fb/open-perception-kit/metadata/person-presences.js';
import { PoseEstimations as PoseEstimations_5268014742632501193, PoseEstimationsT as PoseEstimationsT_5268014742632501193 } from './fb/open-perception-kit/metadata/pose-estimations.js';
import { SegmentationMasks as SegmentationMasks_3128408959875976772, SegmentationMasksT as SegmentationMasksT_3128408959875976772 } from './fb/open-perception-kit/metadata/segmentation-masks.js';
import { SemanticSegmentationMasks as SemanticSegmentationMasks_133049182594848948, SemanticSegmentationMasksT as SemanticSegmentationMasksT_133049182594848948 } from './fb/open-perception-kit/metadata/semantic-segmentation-masks.js';
import { TrackTraces as TrackTraces_7618457940451616939, TrackTracesT as TrackTracesT_7618457940451616939 } from './fb/open-perception-kit/metadata/track-traces.js';
import { TurnDetections as TurnDetections_7194963704333257544, TurnDetectionsT as TurnDetectionsT_7194963704333257544 } from './fb/open-perception-kit/metadata/turn-detections.js';
import { VoiceActivityDetections as VoiceActivityDetections_4750671262425495264, VoiceActivityDetectionsT as VoiceActivityDetectionsT_4750671262425495264 } from './fb/open-perception-kit/metadata/voice-activity-detections.js';
export type PayloadClass<T = unknown> = Function & { prototype: T };

export type TypeInfo = {
  name: string;
  root_type: string;
  qualified_root_type: string;
  file_identifier: string;
  decode: (blob: Uint8Array) => unknown;
  verify?: (blob: Uint8Array) => boolean;
};

const decode_3248104387212218840 = (blob: Uint8Array): unknown => BoxDetections_3248104387212218840.getRootAsBoxDetections(new ByteBuffer(blob)).unpack();
const verify_3248104387212218840 = (blob: Uint8Array): boolean => BoxDetections_3248104387212218840.bufferHasIdentifier(new ByteBuffer(blob));
const decode_7630654510173219138 = (blob: Uint8Array): unknown => CameraContacts_7630654510173219138.getRootAsCameraContacts(new ByteBuffer(blob)).unpack();
const verify_7630654510173219138 = (blob: Uint8Array): boolean => CameraContacts_7630654510173219138.bufferHasIdentifier(new ByteBuffer(blob));
const decode_3375056508603945320 = (blob: Uint8Array): unknown => Classifications_3375056508603945320.getRootAsClassifications(new ByteBuffer(blob)).unpack();
const verify_3375056508603945320 = (blob: Uint8Array): boolean => Classifications_3375056508603945320.bufferHasIdentifier(new ByteBuffer(blob));
const decode_5246681475592864076 = (blob: Uint8Array): unknown => FrameContext_5246681475592864076.getRootAsFrameContext(new ByteBuffer(blob)).unpack();
const verify_5246681475592864076 = (blob: Uint8Array): boolean => FrameContext_5246681475592864076.bufferHasIdentifier(new ByteBuffer(blob));
const decode_7634499802271603959 = (blob: Uint8Array): unknown => InstanceSegmentationMasks_7634499802271603959.getRootAsInstanceSegmentationMasks(new ByteBuffer(blob)).unpack();
const verify_7634499802271603959 = (blob: Uint8Array): boolean => InstanceSegmentationMasks_7634499802271603959.bufferHasIdentifier(new ByteBuffer(blob));
const decode_6121378805478677067 = (blob: Uint8Array): unknown => ObjectEmbeddings_6121378805478677067.getRootAsObjectEmbeddings(new ByteBuffer(blob)).unpack();
const verify_6121378805478677067 = (blob: Uint8Array): boolean => ObjectEmbeddings_6121378805478677067.bufferHasIdentifier(new ByteBuffer(blob));
const decode_5316059591418463553 = (blob: Uint8Array): unknown => ObjectTracks_5316059591418463553.getRootAsObjectTracks(new ByteBuffer(blob)).unpack();
const verify_5316059591418463553 = (blob: Uint8Array): boolean => ObjectTracks_5316059591418463553.bufferHasIdentifier(new ByteBuffer(blob));
const decode_2630163151625760217 = (blob: Uint8Array): unknown => PerformanceMeasurements_2630163151625760217.getRootAsPerformanceMeasurements(new ByteBuffer(blob)).unpack();
const verify_2630163151625760217 = (blob: Uint8Array): boolean => PerformanceMeasurements_2630163151625760217.bufferHasIdentifier(new ByteBuffer(blob));
const decode_7550522492754457054 = (blob: Uint8Array): unknown => PersonPresences_7550522492754457054.getRootAsPersonPresences(new ByteBuffer(blob)).unpack();
const verify_7550522492754457054 = (blob: Uint8Array): boolean => PersonPresences_7550522492754457054.bufferHasIdentifier(new ByteBuffer(blob));
const decode_5268014742632501193 = (blob: Uint8Array): unknown => PoseEstimations_5268014742632501193.getRootAsPoseEstimations(new ByteBuffer(blob)).unpack();
const verify_5268014742632501193 = (blob: Uint8Array): boolean => PoseEstimations_5268014742632501193.bufferHasIdentifier(new ByteBuffer(blob));
const decode_3128408959875976772 = (blob: Uint8Array): unknown => SegmentationMasks_3128408959875976772.getRootAsSegmentationMasks(new ByteBuffer(blob)).unpack();
const verify_3128408959875976772 = (blob: Uint8Array): boolean => SegmentationMasks_3128408959875976772.bufferHasIdentifier(new ByteBuffer(blob));
const decode_133049182594848948 = (blob: Uint8Array): unknown => SemanticSegmentationMasks_133049182594848948.getRootAsSemanticSegmentationMasks(new ByteBuffer(blob)).unpack();
const verify_133049182594848948 = (blob: Uint8Array): boolean => SemanticSegmentationMasks_133049182594848948.bufferHasIdentifier(new ByteBuffer(blob));
const decode_7618457940451616939 = (blob: Uint8Array): unknown => TrackTraces_7618457940451616939.getRootAsTrackTraces(new ByteBuffer(blob)).unpack();
const verify_7618457940451616939 = (blob: Uint8Array): boolean => TrackTraces_7618457940451616939.bufferHasIdentifier(new ByteBuffer(blob));
const decode_7194963704333257544 = (blob: Uint8Array): unknown => TurnDetections_7194963704333257544.getRootAsTurnDetections(new ByteBuffer(blob)).unpack();
const verify_7194963704333257544 = (blob: Uint8Array): boolean => TurnDetections_7194963704333257544.bufferHasIdentifier(new ByteBuffer(blob));
const decode_4750671262425495264 = (blob: Uint8Array): unknown => VoiceActivityDetections_4750671262425495264.getRootAsVoiceActivityDetections(new ByteBuffer(blob)).unpack();
const verify_4750671262425495264 = (blob: Uint8Array): boolean => VoiceActivityDetections_4750671262425495264.bufferHasIdentifier(new ByteBuffer(blob));

export const _TYPE_REGISTRY = new Map<bigint, TypeInfo>([
  [3248104387212218840n, {
    name: 'open_perception_kit::metadata::BoxDetections',
    root_type: 'BoxDetections',
    qualified_root_type: 'open_perception_kit.metadata.BoxDetections',
    file_identifier: 'BDET',
    decode: decode_3248104387212218840,
    verify: verify_3248104387212218840,
  }],
  [7630654510173219138n, {
    name: 'open_perception_kit::metadata::CameraContacts',
    root_type: 'CameraContacts',
    qualified_root_type: 'open_perception_kit.metadata.CameraContacts',
    file_identifier: 'CCON',
    decode: decode_7630654510173219138,
    verify: verify_7630654510173219138,
  }],
  [3375056508603945320n, {
    name: 'open_perception_kit::metadata::Classifications',
    root_type: 'Classifications',
    qualified_root_type: 'open_perception_kit.metadata.Classifications',
    file_identifier: 'CLSF',
    decode: decode_3375056508603945320,
    verify: verify_3375056508603945320,
  }],
  [5246681475592864076n, {
    name: 'open_perception_kit::metadata::FrameContext',
    root_type: 'FrameContext',
    qualified_root_type: 'open_perception_kit.metadata.FrameContext',
    file_identifier: 'FCTX',
    decode: decode_5246681475592864076,
    verify: verify_5246681475592864076,
  }],
  [7634499802271603959n, {
    name: 'open_perception_kit::metadata::InstanceSegmentationMasks',
    root_type: 'InstanceSegmentationMasks',
    qualified_root_type: 'open_perception_kit.metadata.InstanceSegmentationMasks',
    file_identifier: 'ISGM',
    decode: decode_7634499802271603959,
    verify: verify_7634499802271603959,
  }],
  [6121378805478677067n, {
    name: 'open_perception_kit::metadata::ObjectEmbeddings',
    root_type: 'ObjectEmbeddings',
    qualified_root_type: 'open_perception_kit.metadata.ObjectEmbeddings',
    file_identifier: 'EMBE',
    decode: decode_6121378805478677067,
    verify: verify_6121378805478677067,
  }],
  [5316059591418463553n, {
    name: 'open_perception_kit::metadata::ObjectTracks',
    root_type: 'ObjectTracks',
    qualified_root_type: 'open_perception_kit.metadata.ObjectTracks',
    file_identifier: 'TRKS',
    decode: decode_5316059591418463553,
    verify: verify_5316059591418463553,
  }],
  [2630163151625760217n, {
    name: 'open_perception_kit::metadata::PerformanceMeasurements',
    root_type: 'PerformanceMeasurements',
    qualified_root_type: 'open_perception_kit.metadata.PerformanceMeasurements',
    file_identifier: 'PMET',
    decode: decode_2630163151625760217,
    verify: verify_2630163151625760217,
  }],
  [7550522492754457054n, {
    name: 'open_perception_kit::metadata::PersonPresences',
    root_type: 'PersonPresences',
    qualified_root_type: 'open_perception_kit.metadata.PersonPresences',
    file_identifier: 'PPRS',
    decode: decode_7550522492754457054,
    verify: verify_7550522492754457054,
  }],
  [5268014742632501193n, {
    name: 'open_perception_kit::metadata::PoseEstimations',
    root_type: 'PoseEstimations',
    qualified_root_type: 'open_perception_kit.metadata.PoseEstimations',
    file_identifier: 'POSE',
    decode: decode_5268014742632501193,
    verify: verify_5268014742632501193,
  }],
  [3128408959875976772n, {
    name: 'open_perception_kit::metadata::SegmentationMasks',
    root_type: 'SegmentationMasks',
    qualified_root_type: 'open_perception_kit.metadata.SegmentationMasks',
    file_identifier: 'SGMS',
    decode: decode_3128408959875976772,
    verify: verify_3128408959875976772,
  }],
  [133049182594848948n, {
    name: 'open_perception_kit::metadata::SemanticSegmentationMasks',
    root_type: 'SemanticSegmentationMasks',
    qualified_root_type: 'open_perception_kit.metadata.SemanticSegmentationMasks',
    file_identifier: 'SSGM',
    decode: decode_133049182594848948,
    verify: verify_133049182594848948,
  }],
  [7618457940451616939n, {
    name: 'open_perception_kit::metadata::TrackTraces',
    root_type: 'TrackTraces',
    qualified_root_type: 'open_perception_kit.metadata.TrackTraces',
    file_identifier: 'TRCE',
    decode: decode_7618457940451616939,
    verify: verify_7618457940451616939,
  }],
  [7194963704333257544n, {
    name: 'open_perception_kit::metadata::TurnDetections',
    root_type: 'TurnDetections',
    qualified_root_type: 'open_perception_kit.metadata.TurnDetections',
    file_identifier: 'TURN',
    decode: decode_7194963704333257544,
    verify: verify_7194963704333257544,
  }],
  [4750671262425495264n, {
    name: 'open_perception_kit::metadata::VoiceActivityDetections',
    root_type: 'VoiceActivityDetections',
    qualified_root_type: 'open_perception_kit.metadata.VoiceActivityDetections',
    file_identifier: 'VACT',
    decode: decode_4750671262425495264,
    verify: verify_4750671262425495264,
  }],
]);

export const _CLASS_TO_ID = new Map<PayloadClass, bigint>([
  [BoxDetectionsT_3248104387212218840, 3248104387212218840n],
  [CameraContactsT_7630654510173219138, 7630654510173219138n],
  [ClassificationsT_3375056508603945320, 3375056508603945320n],
  [FrameContextT_5246681475592864076, 5246681475592864076n],
  [InstanceSegmentationMasksT_7634499802271603959, 7634499802271603959n],
  [ObjectEmbeddingsT_6121378805478677067, 6121378805478677067n],
  [ObjectTracksT_5316059591418463553, 5316059591418463553n],
  [PerformanceMeasurementsT_2630163151625760217, 2630163151625760217n],
  [PersonPresencesT_7550522492754457054, 7550522492754457054n],
  [PoseEstimationsT_5268014742632501193, 5268014742632501193n],
  [SegmentationMasksT_3128408959875976772, 3128408959875976772n],
  [SemanticSegmentationMasksT_133049182594848948, 133049182594848948n],
  [TrackTracesT_7618457940451616939, 7618457940451616939n],
  [TurnDetectionsT_7194963704333257544, 7194963704333257544n],
  [VoiceActivityDetectionsT_4750671262425495264, 4750671262425495264n],
]);