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
import * as flatbuffers from 'flatbuffers';
import { ClassificationCandidate, ClassificationCandidateT } from '../../open-perception-kit/metadata/classification-candidate.js';
import { ClassificationKind } from '../../open-perception-kit/metadata/classification-kind.js';
import { ObjectMeta, ObjectMetaT } from '../../open-perception-kit/metadata/object-meta.js';
export declare class Classification implements flatbuffers.IUnpackableObject<ClassificationT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): Classification;
    static getRootAsClassification(bb: flatbuffers.ByteBuffer, obj?: Classification): Classification;
    static getSizePrefixedRootAsClassification(bb: flatbuffers.ByteBuffer, obj?: Classification): Classification;
    object(obj?: ObjectMeta): ObjectMeta | null;
    candidates(index: number, obj?: ClassificationCandidate): ClassificationCandidate | null;
    candidatesLength(): number;
    labelSet(): string | null;
    labelSet(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    kind(): ClassificationKind;
    subjectId(): bigint | null;
    static startClassification(builder: flatbuffers.Builder): void;
    static addObject(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset): void;
    static addCandidates(builder: flatbuffers.Builder, candidatesOffset: flatbuffers.Offset): void;
    static createCandidatesVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startCandidatesVector(builder: flatbuffers.Builder, numElems: number): void;
    static addLabelSet(builder: flatbuffers.Builder, labelSetOffset: flatbuffers.Offset): void;
    static addKind(builder: flatbuffers.Builder, kind: ClassificationKind): void;
    static addSubjectId(builder: flatbuffers.Builder, subjectId: bigint): void;
    static endClassification(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createClassification(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset, candidatesOffset: flatbuffers.Offset, labelSetOffset: flatbuffers.Offset, kind: ClassificationKind, subjectId: bigint | null): flatbuffers.Offset;
    unpack(): ClassificationT;
    unpackTo(_o: ClassificationT): void;
}
export declare class ClassificationT implements flatbuffers.IGeneratedObject {
    object: ObjectMetaT | null;
    candidates: (ClassificationCandidateT)[];
    labelSet: string | Uint8Array | null;
    kind: ClassificationKind;
    subjectId: bigint | null;
    constructor(object?: ObjectMetaT | null, candidates?: (ClassificationCandidateT)[], labelSet?: string | Uint8Array | null, kind?: ClassificationKind, subjectId?: bigint | null);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
