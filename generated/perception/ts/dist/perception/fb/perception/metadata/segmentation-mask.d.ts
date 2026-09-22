// Copyright (C) 2026 Arm Limited. All rights reserved.
// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.
import * as flatbuffers from 'flatbuffers';
import { BitmapData, BitmapDataT } from '../../perception/metadata/bitmap-data.js';
import { ObjectMeta, ObjectMetaT } from '../../perception/metadata/object-meta.js';
export declare class SegmentationMask implements flatbuffers.IUnpackableObject<SegmentationMaskT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): SegmentationMask;
    static getRootAsSegmentationMask(bb: flatbuffers.ByteBuffer, obj?: SegmentationMask): SegmentationMask;
    static getSizePrefixedRootAsSegmentationMask(bb: flatbuffers.ByteBuffer, obj?: SegmentationMask): SegmentationMask;
    object(obj?: ObjectMeta): ObjectMeta | null;
    bitmap(obj?: BitmapData): BitmapData | null;
    static startSegmentationMask(builder: flatbuffers.Builder): void;
    static addObject(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset): void;
    static addBitmap(builder: flatbuffers.Builder, bitmapOffset: flatbuffers.Offset): void;
    static endSegmentationMask(builder: flatbuffers.Builder): flatbuffers.Offset;
    unpack(): SegmentationMaskT;
    unpackTo(_o: SegmentationMaskT): void;
}
export declare class SegmentationMaskT implements flatbuffers.IGeneratedObject {
    object: ObjectMetaT | null;
    bitmap: BitmapDataT | null;
    constructor(object?: ObjectMetaT | null, bitmap?: BitmapDataT | null);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
