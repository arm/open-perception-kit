// Copyright (C) 2026 Arm Limited. All rights reserved.
// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.
import * as flatbuffers from 'flatbuffers';
import { ObjectMeta, ObjectMetaT } from '../../open-perception-kit/metadata/object-meta.js';
import { Point2f, Point2fT } from '../../open-perception-kit/metadata/point2f.js';
export declare class TrackTrace implements flatbuffers.IUnpackableObject<TrackTraceT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): TrackTrace;
    static getRootAsTrackTrace(bb: flatbuffers.ByteBuffer, obj?: TrackTrace): TrackTrace;
    static getSizePrefixedRootAsTrackTrace(bb: flatbuffers.ByteBuffer, obj?: TrackTrace): TrackTrace;
    object(obj?: ObjectMeta): ObjectMeta | null;
    trackId(): bigint;
    points(index: number, obj?: Point2f): Point2f | null;
    pointsLength(): number;
    static startTrackTrace(builder: flatbuffers.Builder): void;
    static addObject(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset): void;
    static addTrackId(builder: flatbuffers.Builder, trackId: bigint): void;
    static addPoints(builder: flatbuffers.Builder, pointsOffset: flatbuffers.Offset): void;
    static createPointsVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startPointsVector(builder: flatbuffers.Builder, numElems: number): void;
    static endTrackTrace(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createTrackTrace(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset, trackId: bigint, pointsOffset: flatbuffers.Offset): flatbuffers.Offset;
    unpack(): TrackTraceT;
    unpackTo(_o: TrackTraceT): void;
}
export declare class TrackTraceT implements flatbuffers.IGeneratedObject {
    object: ObjectMetaT | null;
    trackId: bigint;
    points: (Point2fT)[];
    constructor(object?: ObjectMetaT | null, trackId?: bigint, points?: (Point2fT)[]);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
