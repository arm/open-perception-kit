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
import * as flatbuffers from 'flatbuffers';
import { ClassificationCandidate, ClassificationCandidateT } from '../../open-perception-kit/metadata/classification-candidate.js';
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
