/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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
import { BoxDetections as BoxDetections_556103652012567315, BoxDetectionsT as BoxDetectionsT_556103652012567315 } from './fb/open-perception-kit/metadata/box-detections.js';
import { Classifications as Classifications_2852697023809600655, ClassificationsT as ClassificationsT_2852697023809600655 } from './fb/open-perception-kit/metadata/classifications.js';
import { FrameContext as FrameContext_2065478860695108412, FrameContextT as FrameContextT_2065478860695108412 } from './fb/open-perception-kit/metadata/frame-context.js';
import { ObjectEmbeddings as ObjectEmbeddings_5972661533224817501, ObjectEmbeddingsT as ObjectEmbeddingsT_5972661533224817501 } from './fb/open-perception-kit/metadata/object-embeddings.js';
import { ObjectTracks as ObjectTracks_2960585987463094496, ObjectTracksT as ObjectTracksT_2960585987463094496 } from './fb/open-perception-kit/metadata/object-tracks.js';
import { PerformanceOverlay as PerformanceOverlay_7749401259036278028, PerformanceOverlayT as PerformanceOverlayT_7749401259036278028 } from './fb/open-perception-kit/metadata/performance-overlay.js';
import { PoseEstimations as PoseEstimations_9114952555105892553, PoseEstimationsT as PoseEstimationsT_9114952555105892553 } from './fb/open-perception-kit/metadata/pose-estimations.js';
import { SegmentationMasks as SegmentationMasks_1998909987238011535, SegmentationMasksT as SegmentationMasksT_1998909987238011535 } from './fb/open-perception-kit/metadata/segmentation-masks.js';
import { TrackTraces as TrackTraces_238211229389337861, TrackTracesT as TrackTracesT_238211229389337861 } from './fb/open-perception-kit/metadata/track-traces.js';
export type PayloadClass<T = unknown> = Function & { prototype: T };

export type TypeInfo = {
  name: string;
  root_type: string;
  qualified_root_type: string;
  file_identifier: string;
  decode: (blob: Uint8Array) => unknown;
  verify?: (blob: Uint8Array) => boolean;
};

const decode_556103652012567315 = (blob: Uint8Array): unknown => BoxDetections_556103652012567315.getRootAsBoxDetections(new ByteBuffer(blob)).unpack();
const verify_556103652012567315 = (blob: Uint8Array): boolean => BoxDetections_556103652012567315.bufferHasIdentifier(new ByteBuffer(blob));
const decode_2852697023809600655 = (blob: Uint8Array): unknown => Classifications_2852697023809600655.getRootAsClassifications(new ByteBuffer(blob)).unpack();
const verify_2852697023809600655 = (blob: Uint8Array): boolean => Classifications_2852697023809600655.bufferHasIdentifier(new ByteBuffer(blob));
const decode_2065478860695108412 = (blob: Uint8Array): unknown => FrameContext_2065478860695108412.getRootAsFrameContext(new ByteBuffer(blob)).unpack();
const verify_2065478860695108412 = (blob: Uint8Array): boolean => FrameContext_2065478860695108412.bufferHasIdentifier(new ByteBuffer(blob));
const decode_5972661533224817501 = (blob: Uint8Array): unknown => ObjectEmbeddings_5972661533224817501.getRootAsObjectEmbeddings(new ByteBuffer(blob)).unpack();
const verify_5972661533224817501 = (blob: Uint8Array): boolean => ObjectEmbeddings_5972661533224817501.bufferHasIdentifier(new ByteBuffer(blob));
const decode_2960585987463094496 = (blob: Uint8Array): unknown => ObjectTracks_2960585987463094496.getRootAsObjectTracks(new ByteBuffer(blob)).unpack();
const verify_2960585987463094496 = (blob: Uint8Array): boolean => ObjectTracks_2960585987463094496.bufferHasIdentifier(new ByteBuffer(blob));
const decode_7749401259036278028 = (blob: Uint8Array): unknown => PerformanceOverlay_7749401259036278028.getRootAsPerformanceOverlay(new ByteBuffer(blob)).unpack();
const verify_7749401259036278028 = (blob: Uint8Array): boolean => PerformanceOverlay_7749401259036278028.bufferHasIdentifier(new ByteBuffer(blob));
const decode_9114952555105892553 = (blob: Uint8Array): unknown => PoseEstimations_9114952555105892553.getRootAsPoseEstimations(new ByteBuffer(blob)).unpack();
const verify_9114952555105892553 = (blob: Uint8Array): boolean => PoseEstimations_9114952555105892553.bufferHasIdentifier(new ByteBuffer(blob));
const decode_1998909987238011535 = (blob: Uint8Array): unknown => SegmentationMasks_1998909987238011535.getRootAsSegmentationMasks(new ByteBuffer(blob)).unpack();
const verify_1998909987238011535 = (blob: Uint8Array): boolean => SegmentationMasks_1998909987238011535.bufferHasIdentifier(new ByteBuffer(blob));
const decode_238211229389337861 = (blob: Uint8Array): unknown => TrackTraces_238211229389337861.getRootAsTrackTraces(new ByteBuffer(blob)).unpack();
const verify_238211229389337861 = (blob: Uint8Array): boolean => TrackTraces_238211229389337861.bufferHasIdentifier(new ByteBuffer(blob));

export const _TYPE_REGISTRY = new Map<bigint, TypeInfo>([
  [556103652012567315n, {
    name: 'open_perception_kit::metadata::BoxDetections',
    root_type: 'BoxDetections',
    qualified_root_type: 'open_perception_kit.metadata.BoxDetections',
    file_identifier: 'BDET',
    decode: decode_556103652012567315,
    verify: verify_556103652012567315,
  }],
  [2852697023809600655n, {
    name: 'open_perception_kit::metadata::Classifications',
    root_type: 'Classifications',
    qualified_root_type: 'open_perception_kit.metadata.Classifications',
    file_identifier: 'CLSF',
    decode: decode_2852697023809600655,
    verify: verify_2852697023809600655,
  }],
  [2065478860695108412n, {
    name: 'open_perception_kit::metadata::FrameContext',
    root_type: 'FrameContext',
    qualified_root_type: 'open_perception_kit.metadata.FrameContext',
    file_identifier: 'FCTX',
    decode: decode_2065478860695108412,
    verify: verify_2065478860695108412,
  }],
  [5972661533224817501n, {
    name: 'open_perception_kit::metadata::ObjectEmbeddings',
    root_type: 'ObjectEmbeddings',
    qualified_root_type: 'open_perception_kit.metadata.ObjectEmbeddings',
    file_identifier: 'EMBE',
    decode: decode_5972661533224817501,
    verify: verify_5972661533224817501,
  }],
  [2960585987463094496n, {
    name: 'open_perception_kit::metadata::ObjectTracks',
    root_type: 'ObjectTracks',
    qualified_root_type: 'open_perception_kit.metadata.ObjectTracks',
    file_identifier: 'TRKS',
    decode: decode_2960585987463094496,
    verify: verify_2960585987463094496,
  }],
  [7749401259036278028n, {
    name: 'open_perception_kit::metadata::PerformanceOverlay',
    root_type: 'PerformanceOverlay',
    qualified_root_type: 'open_perception_kit.metadata.PerformanceOverlay',
    file_identifier: 'PERF',
    decode: decode_7749401259036278028,
    verify: verify_7749401259036278028,
  }],
  [9114952555105892553n, {
    name: 'open_perception_kit::metadata::PoseEstimations',
    root_type: 'PoseEstimations',
    qualified_root_type: 'open_perception_kit.metadata.PoseEstimations',
    file_identifier: 'POSE',
    decode: decode_9114952555105892553,
    verify: verify_9114952555105892553,
  }],
  [1998909987238011535n, {
    name: 'open_perception_kit::metadata::SegmentationMasks',
    root_type: 'SegmentationMasks',
    qualified_root_type: 'open_perception_kit.metadata.SegmentationMasks',
    file_identifier: 'SGMS',
    decode: decode_1998909987238011535,
    verify: verify_1998909987238011535,
  }],
  [238211229389337861n, {
    name: 'open_perception_kit::metadata::TrackTraces',
    root_type: 'TrackTraces',
    qualified_root_type: 'open_perception_kit.metadata.TrackTraces',
    file_identifier: 'TRCE',
    decode: decode_238211229389337861,
    verify: verify_238211229389337861,
  }],
]);

export const _CLASS_TO_ID = new Map<PayloadClass, bigint>([
  [BoxDetectionsT_556103652012567315, 556103652012567315n],
  [ClassificationsT_2852697023809600655, 2852697023809600655n],
  [FrameContextT_2065478860695108412, 2065478860695108412n],
  [ObjectEmbeddingsT_5972661533224817501, 5972661533224817501n],
  [ObjectTracksT_2960585987463094496, 2960585987463094496n],
  [PerformanceOverlayT_7749401259036278028, 7749401259036278028n],
  [PoseEstimationsT_9114952555105892553, 9114952555105892553n],
  [SegmentationMasksT_1998909987238011535, 1998909987238011535n],
  [TrackTracesT_238211229389337861, 238211229389337861n],
]);