// Copyright (C) 2026 Arm Limited. All rights reserved.
// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.
import { ByteBuffer } from 'flatbuffers';
import { BoxDetections as BoxDetections_928609632921539799, BoxDetectionsT as BoxDetectionsT_928609632921539799 } from './fb/perception/metadata/box-detections.js';
import { Classifications as Classifications_94127366257443529, ClassificationsT as ClassificationsT_94127366257443529 } from './fb/perception/metadata/classifications.js';
import { FrameContext as FrameContext_6405170853304169454, FrameContextT as FrameContextT_6405170853304169454 } from './fb/perception/metadata/frame-context.js';
import { ObjectEmbeddings as ObjectEmbeddings_3474598619102273931, ObjectEmbeddingsT as ObjectEmbeddingsT_3474598619102273931 } from './fb/perception/metadata/object-embeddings.js';
import { ObjectTracks as ObjectTracks_930392077708082693, ObjectTracksT as ObjectTracksT_930392077708082693 } from './fb/perception/metadata/object-tracks.js';
import { PerformanceOverlay as PerformanceOverlay_4179744154867129599, PerformanceOverlayT as PerformanceOverlayT_4179744154867129599 } from './fb/perception/metadata/performance-overlay.js';
import { PoseEstimations as PoseEstimations_8795139052133278924, PoseEstimationsT as PoseEstimationsT_8795139052133278924 } from './fb/perception/metadata/pose-estimations.js';
import { SegmentationMasks as SegmentationMasks_1102215109093226736, SegmentationMasksT as SegmentationMasksT_1102215109093226736 } from './fb/perception/metadata/segmentation-masks.js';
import { TrackTraces as TrackTraces_8745337222662207869, TrackTracesT as TrackTracesT_8745337222662207869 } from './fb/perception/metadata/track-traces.js';
const decode_928609632921539799 = (blob) => BoxDetections_928609632921539799.getRootAsBoxDetections(new ByteBuffer(blob)).unpack();
const verify_928609632921539799 = (blob) => BoxDetections_928609632921539799.bufferHasIdentifier(new ByteBuffer(blob));
const decode_94127366257443529 = (blob) => Classifications_94127366257443529.getRootAsClassifications(new ByteBuffer(blob)).unpack();
const verify_94127366257443529 = (blob) => Classifications_94127366257443529.bufferHasIdentifier(new ByteBuffer(blob));
const decode_6405170853304169454 = (blob) => FrameContext_6405170853304169454.getRootAsFrameContext(new ByteBuffer(blob)).unpack();
const verify_6405170853304169454 = (blob) => FrameContext_6405170853304169454.bufferHasIdentifier(new ByteBuffer(blob));
const decode_3474598619102273931 = (blob) => ObjectEmbeddings_3474598619102273931.getRootAsObjectEmbeddings(new ByteBuffer(blob)).unpack();
const verify_3474598619102273931 = (blob) => ObjectEmbeddings_3474598619102273931.bufferHasIdentifier(new ByteBuffer(blob));
const decode_930392077708082693 = (blob) => ObjectTracks_930392077708082693.getRootAsObjectTracks(new ByteBuffer(blob)).unpack();
const verify_930392077708082693 = (blob) => ObjectTracks_930392077708082693.bufferHasIdentifier(new ByteBuffer(blob));
const decode_4179744154867129599 = (blob) => PerformanceOverlay_4179744154867129599.getRootAsPerformanceOverlay(new ByteBuffer(blob)).unpack();
const verify_4179744154867129599 = (blob) => PerformanceOverlay_4179744154867129599.bufferHasIdentifier(new ByteBuffer(blob));
const decode_8795139052133278924 = (blob) => PoseEstimations_8795139052133278924.getRootAsPoseEstimations(new ByteBuffer(blob)).unpack();
const verify_8795139052133278924 = (blob) => PoseEstimations_8795139052133278924.bufferHasIdentifier(new ByteBuffer(blob));
const decode_1102215109093226736 = (blob) => SegmentationMasks_1102215109093226736.getRootAsSegmentationMasks(new ByteBuffer(blob)).unpack();
const verify_1102215109093226736 = (blob) => SegmentationMasks_1102215109093226736.bufferHasIdentifier(new ByteBuffer(blob));
const decode_8745337222662207869 = (blob) => TrackTraces_8745337222662207869.getRootAsTrackTraces(new ByteBuffer(blob)).unpack();
const verify_8745337222662207869 = (blob) => TrackTraces_8745337222662207869.bufferHasIdentifier(new ByteBuffer(blob));
export const _TYPE_REGISTRY = new Map([
    [928609632921539799n, {
            name: 'perception::metadata::BoxDetections',
            root_type: 'BoxDetections',
            qualified_root_type: 'perception.metadata.BoxDetections',
            file_identifier: 'BDET',
            decode: decode_928609632921539799,
            verify: verify_928609632921539799,
        }],
    [94127366257443529n, {
            name: 'perception::metadata::Classifications',
            root_type: 'Classifications',
            qualified_root_type: 'perception.metadata.Classifications',
            file_identifier: 'CLSF',
            decode: decode_94127366257443529,
            verify: verify_94127366257443529,
        }],
    [6405170853304169454n, {
            name: 'perception::metadata::FrameContext',
            root_type: 'FrameContext',
            qualified_root_type: 'perception.metadata.FrameContext',
            file_identifier: 'FCTX',
            decode: decode_6405170853304169454,
            verify: verify_6405170853304169454,
        }],
    [3474598619102273931n, {
            name: 'perception::metadata::ObjectEmbeddings',
            root_type: 'ObjectEmbeddings',
            qualified_root_type: 'perception.metadata.ObjectEmbeddings',
            file_identifier: 'EMBE',
            decode: decode_3474598619102273931,
            verify: verify_3474598619102273931,
        }],
    [930392077708082693n, {
            name: 'perception::metadata::ObjectTracks',
            root_type: 'ObjectTracks',
            qualified_root_type: 'perception.metadata.ObjectTracks',
            file_identifier: 'TRKS',
            decode: decode_930392077708082693,
            verify: verify_930392077708082693,
        }],
    [4179744154867129599n, {
            name: 'perception::metadata::PerformanceOverlay',
            root_type: 'PerformanceOverlay',
            qualified_root_type: 'perception.metadata.PerformanceOverlay',
            file_identifier: 'PERF',
            decode: decode_4179744154867129599,
            verify: verify_4179744154867129599,
        }],
    [8795139052133278924n, {
            name: 'perception::metadata::PoseEstimations',
            root_type: 'PoseEstimations',
            qualified_root_type: 'perception.metadata.PoseEstimations',
            file_identifier: 'POSE',
            decode: decode_8795139052133278924,
            verify: verify_8795139052133278924,
        }],
    [1102215109093226736n, {
            name: 'perception::metadata::SegmentationMasks',
            root_type: 'SegmentationMasks',
            qualified_root_type: 'perception.metadata.SegmentationMasks',
            file_identifier: 'SGMS',
            decode: decode_1102215109093226736,
            verify: verify_1102215109093226736,
        }],
    [8745337222662207869n, {
            name: 'perception::metadata::TrackTraces',
            root_type: 'TrackTraces',
            qualified_root_type: 'perception.metadata.TrackTraces',
            file_identifier: 'TRCE',
            decode: decode_8745337222662207869,
            verify: verify_8745337222662207869,
        }],
]);
export const _CLASS_TO_ID = new Map([
    [BoxDetectionsT_928609632921539799, 928609632921539799n],
    [ClassificationsT_94127366257443529, 94127366257443529n],
    [FrameContextT_6405170853304169454, 6405170853304169454n],
    [ObjectEmbeddingsT_3474598619102273931, 3474598619102273931n],
    [ObjectTracksT_930392077708082693, 930392077708082693n],
    [PerformanceOverlayT_4179744154867129599, 4179744154867129599n],
    [PoseEstimationsT_8795139052133278924, 8795139052133278924n],
    [SegmentationMasksT_1102215109093226736, 1102215109093226736n],
    [TrackTracesT_8745337222662207869, 8745337222662207869n],
]);
