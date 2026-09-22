// Copyright (C) 2026 Arm Limited. All rights reserved.
// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.
import * as flatbuffers from 'flatbuffers';
import { ClassificationCandidate, ClassificationCandidateT } from '../../perception/metadata/classification-candidate.js';
import { ObjectMeta, ObjectMetaT } from '../../perception/metadata/object-meta.js';
export declare class Classification implements flatbuffers.IUnpackableObject<ClassificationT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): Classification;
    static getRootAsClassification(bb: flatbuffers.ByteBuffer, obj?: Classification): Classification;
    static getSizePrefixedRootAsClassification(bb: flatbuffers.ByteBuffer, obj?: Classification): Classification;
    object(obj?: ObjectMeta): ObjectMeta | null;
    candidates(index: number, obj?: ClassificationCandidate): ClassificationCandidate | null;
    candidatesLength(): number;
    static startClassification(builder: flatbuffers.Builder): void;
    static addObject(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset): void;
    static addCandidates(builder: flatbuffers.Builder, candidatesOffset: flatbuffers.Offset): void;
    static createCandidatesVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startCandidatesVector(builder: flatbuffers.Builder, numElems: number): void;
    static endClassification(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createClassification(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset, candidatesOffset: flatbuffers.Offset): flatbuffers.Offset;
    unpack(): ClassificationT;
    unpackTo(_o: ClassificationT): void;
}
export declare class ClassificationT implements flatbuffers.IGeneratedObject {
    object: ObjectMetaT | null;
    candidates: (ClassificationCandidateT)[];
    constructor(object?: ObjectMetaT | null, candidates?: (ClassificationCandidateT)[]);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
