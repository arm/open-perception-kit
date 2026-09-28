// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.
import * as flatbuffers from 'flatbuffers';
export declare class BoundingBox implements flatbuffers.IUnpackableObject<BoundingBoxT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): BoundingBox;
    static getRootAsBoundingBox(bb: flatbuffers.ByteBuffer, obj?: BoundingBox): BoundingBox;
    static getSizePrefixedRootAsBoundingBox(bb: flatbuffers.ByteBuffer, obj?: BoundingBox): BoundingBox;
    x(): number;
    y(): number;
    width(): number;
    height(): number;
    static startBoundingBox(builder: flatbuffers.Builder): void;
    static addX(builder: flatbuffers.Builder, x: number): void;
    static addY(builder: flatbuffers.Builder, y: number): void;
    static addWidth(builder: flatbuffers.Builder, width: number): void;
    static addHeight(builder: flatbuffers.Builder, height: number): void;
    static endBoundingBox(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createBoundingBox(builder: flatbuffers.Builder, x: number, y: number, width: number, height: number): flatbuffers.Offset;
    unpack(): BoundingBoxT;
    unpackTo(_o: BoundingBoxT): void;
}
export declare class BoundingBoxT implements flatbuffers.IGeneratedObject {
    x: number;
    y: number;
    width: number;
    height: number;
    constructor(x?: number, y?: number, width?: number, height?: number);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
