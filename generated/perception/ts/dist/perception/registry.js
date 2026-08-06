// Copyright (C) 2025 Arm Limited. All rights reserved.
// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.
import { ByteBuffer } from 'flatbuffers';
import { BoxDetections as BoxDetections_127096183275957372, BoxDetectionsT as BoxDetectionsT_127096183275957372 } from './fb/perception/metadata/box-detections.js';
import { Classifications as Classifications_9181357636124419217, ClassificationsT as ClassificationsT_9181357636124419217 } from './fb/perception/metadata/classifications.js';
import { FrameContext as FrameContext_6787725252958650128, FrameContextT as FrameContextT_6787725252958650128 } from './fb/perception/metadata/frame-context.js';
import { ObjectEmbeddings as ObjectEmbeddings_3601053540183530964, ObjectEmbeddingsT as ObjectEmbeddingsT_3601053540183530964 } from './fb/perception/metadata/object-embeddings.js';
import { ObjectTracks as ObjectTracks_1204340903431744882, ObjectTracksT as ObjectTracksT_1204340903431744882 } from './fb/perception/metadata/object-tracks.js';
import { PerformanceOverlay as PerformanceOverlay_4179744154867129599, PerformanceOverlayT as PerformanceOverlayT_4179744154867129599 } from './fb/perception/metadata/performance-overlay.js';
import { PoseEstimations as PoseEstimations_6089861490284108552, PoseEstimationsT as PoseEstimationsT_6089861490284108552 } from './fb/perception/metadata/pose-estimations.js';
import { SegmentationMasks as SegmentationMasks_3767952910034633902, SegmentationMasksT as SegmentationMasksT_3767952910034633902 } from './fb/perception/metadata/segmentation-masks.js';
import { TrackTraces as TrackTraces_4937615646931894804, TrackTracesT as TrackTracesT_4937615646931894804 } from './fb/perception/metadata/track-traces.js';
const decode_127096183275957372 = (blob) => BoxDetections_127096183275957372.getRootAsBoxDetections(new ByteBuffer(blob)).unpack();
const verify_127096183275957372 = (blob) => BoxDetections_127096183275957372.bufferHasIdentifier(new ByteBuffer(blob));
const decode_9181357636124419217 = (blob) => Classifications_9181357636124419217.getRootAsClassifications(new ByteBuffer(blob)).unpack();
const verify_9181357636124419217 = (blob) => Classifications_9181357636124419217.bufferHasIdentifier(new ByteBuffer(blob));
const decode_6787725252958650128 = (blob) => FrameContext_6787725252958650128.getRootAsFrameContext(new ByteBuffer(blob)).unpack();
const verify_6787725252958650128 = (blob) => FrameContext_6787725252958650128.bufferHasIdentifier(new ByteBuffer(blob));
const decode_3601053540183530964 = (blob) => ObjectEmbeddings_3601053540183530964.getRootAsObjectEmbeddings(new ByteBuffer(blob)).unpack();
const verify_3601053540183530964 = (blob) => ObjectEmbeddings_3601053540183530964.bufferHasIdentifier(new ByteBuffer(blob));
const decode_1204340903431744882 = (blob) => ObjectTracks_1204340903431744882.getRootAsObjectTracks(new ByteBuffer(blob)).unpack();
const verify_1204340903431744882 = (blob) => ObjectTracks_1204340903431744882.bufferHasIdentifier(new ByteBuffer(blob));
const decode_4179744154867129599 = (blob) => PerformanceOverlay_4179744154867129599.getRootAsPerformanceOverlay(new ByteBuffer(blob)).unpack();
const verify_4179744154867129599 = (blob) => PerformanceOverlay_4179744154867129599.bufferHasIdentifier(new ByteBuffer(blob));
const decode_6089861490284108552 = (blob) => PoseEstimations_6089861490284108552.getRootAsPoseEstimations(new ByteBuffer(blob)).unpack();
const verify_6089861490284108552 = (blob) => PoseEstimations_6089861490284108552.bufferHasIdentifier(new ByteBuffer(blob));
const decode_3767952910034633902 = (blob) => SegmentationMasks_3767952910034633902.getRootAsSegmentationMasks(new ByteBuffer(blob)).unpack();
const verify_3767952910034633902 = (blob) => SegmentationMasks_3767952910034633902.bufferHasIdentifier(new ByteBuffer(blob));
const decode_4937615646931894804 = (blob) => TrackTraces_4937615646931894804.getRootAsTrackTraces(new ByteBuffer(blob)).unpack();
const verify_4937615646931894804 = (blob) => TrackTraces_4937615646931894804.bufferHasIdentifier(new ByteBuffer(blob));
export const _TYPE_REGISTRY = new Map([
    [127096183275957372n, {
            name: 'perception::metadata::BoxDetections',
            root_type: 'BoxDetections',
            qualified_root_type: 'perception.metadata.BoxDetections',
            file_identifier: 'BDET',
            decode: decode_127096183275957372,
            verify: verify_127096183275957372,
        }],
    [9181357636124419217n, {
            name: 'perception::metadata::Classifications',
            root_type: 'Classifications',
            qualified_root_type: 'perception.metadata.Classifications',
            file_identifier: 'CLSF',
            decode: decode_9181357636124419217,
            verify: verify_9181357636124419217,
        }],
    [6787725252958650128n, {
            name: 'perception::metadata::FrameContext',
            root_type: 'FrameContext',
            qualified_root_type: 'perception.metadata.FrameContext',
            file_identifier: 'FCTX',
            decode: decode_6787725252958650128,
            verify: verify_6787725252958650128,
        }],
    [3601053540183530964n, {
            name: 'perception::metadata::ObjectEmbeddings',
            root_type: 'ObjectEmbeddings',
            qualified_root_type: 'perception.metadata.ObjectEmbeddings',
            file_identifier: 'EMBE',
            decode: decode_3601053540183530964,
            verify: verify_3601053540183530964,
        }],
    [1204340903431744882n, {
            name: 'perception::metadata::ObjectTracks',
            root_type: 'ObjectTracks',
            qualified_root_type: 'perception.metadata.ObjectTracks',
            file_identifier: 'TRKS',
            decode: decode_1204340903431744882,
            verify: verify_1204340903431744882,
        }],
    [4179744154867129599n, {
            name: 'perception::metadata::PerformanceOverlay',
            root_type: 'PerformanceOverlay',
            qualified_root_type: 'perception.metadata.PerformanceOverlay',
            file_identifier: 'PERF',
            decode: decode_4179744154867129599,
            verify: verify_4179744154867129599,
        }],
    [6089861490284108552n, {
            name: 'perception::metadata::PoseEstimations',
            root_type: 'PoseEstimations',
            qualified_root_type: 'perception.metadata.PoseEstimations',
            file_identifier: 'POSE',
            decode: decode_6089861490284108552,
            verify: verify_6089861490284108552,
        }],
    [3767952910034633902n, {
            name: 'perception::metadata::SegmentationMasks',
            root_type: 'SegmentationMasks',
            qualified_root_type: 'perception.metadata.SegmentationMasks',
            file_identifier: 'SGMS',
            decode: decode_3767952910034633902,
            verify: verify_3767952910034633902,
        }],
    [4937615646931894804n, {
            name: 'perception::metadata::TrackTraces',
            root_type: 'TrackTraces',
            qualified_root_type: 'perception.metadata.TrackTraces',
            file_identifier: 'TRCE',
            decode: decode_4937615646931894804,
            verify: verify_4937615646931894804,
        }],
]);
export const _CLASS_TO_ID = new Map([
    [BoxDetectionsT_127096183275957372, 127096183275957372n],
    [ClassificationsT_9181357636124419217, 9181357636124419217n],
    [FrameContextT_6787725252958650128, 6787725252958650128n],
    [ObjectEmbeddingsT_3601053540183530964, 3601053540183530964n],
    [ObjectTracksT_1204340903431744882, 1204340903431744882n],
    [PerformanceOverlayT_4179744154867129599, 4179744154867129599n],
    [PoseEstimationsT_6089861490284108552, 6089861490284108552n],
    [SegmentationMasksT_3767952910034633902, 3767952910034633902n],
    [TrackTracesT_4937615646931894804, 4937615646931894804n],
]);
