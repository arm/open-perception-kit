// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.
import * as flatbuffers from 'flatbuffers';
import { BoundingBox, BoundingBoxT } from '../../open-perception-kit/metadata/bounding-box.js';
import { ObjectMeta, ObjectMetaT } from '../../open-perception-kit/metadata/object-meta.js';
export declare class BoxDetection implements flatbuffers.IUnpackableObject<BoxDetectionT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): BoxDetection;
    static getRootAsBoxDetection(bb: flatbuffers.ByteBuffer, obj?: BoxDetection): BoxDetection;
    static getSizePrefixedRootAsBoxDetection(bb: flatbuffers.ByteBuffer, obj?: BoxDetection): BoxDetection;
    object(obj?: ObjectMeta): ObjectMeta | null;
    box(obj?: BoundingBox): BoundingBox | null;
    confidence(): number;
    classId(): number;
    text(): string | null;
    text(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    static startBoxDetection(builder: flatbuffers.Builder): void;
    static addObject(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset): void;
    static addBox(builder: flatbuffers.Builder, boxOffset: flatbuffers.Offset): void;
    static addConfidence(builder: flatbuffers.Builder, confidence: number): void;
    static addClassId(builder: flatbuffers.Builder, classId: number): void;
    static addText(builder: flatbuffers.Builder, textOffset: flatbuffers.Offset): void;
    static endBoxDetection(builder: flatbuffers.Builder): flatbuffers.Offset;
    unpack(): BoxDetectionT;
    unpackTo(_o: BoxDetectionT): void;
}
export declare class BoxDetectionT implements flatbuffers.IGeneratedObject {
    object: ObjectMetaT | null;
    box: BoundingBoxT | null;
    confidence: number;
    classId: number;
    text: string | Uint8Array | null;
    constructor(object?: ObjectMetaT | null, box?: BoundingBoxT | null, confidence?: number, classId?: number, text?: string | Uint8Array | null);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
