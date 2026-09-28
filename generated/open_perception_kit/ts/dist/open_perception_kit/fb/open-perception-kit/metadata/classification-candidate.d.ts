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
export declare class ClassificationCandidate implements flatbuffers.IUnpackableObject<ClassificationCandidateT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): ClassificationCandidate;
    static getRootAsClassificationCandidate(bb: flatbuffers.ByteBuffer, obj?: ClassificationCandidate): ClassificationCandidate;
    static getSizePrefixedRootAsClassificationCandidate(bb: flatbuffers.ByteBuffer, obj?: ClassificationCandidate): ClassificationCandidate;
    confidence(): number;
    classId(): number;
    text(): string | null;
    text(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    x(): number;
    y(): number;
    w(): number;
    h(): number;
    static startClassificationCandidate(builder: flatbuffers.Builder): void;
    static addConfidence(builder: flatbuffers.Builder, confidence: number): void;
    static addClassId(builder: flatbuffers.Builder, classId: number): void;
    static addText(builder: flatbuffers.Builder, textOffset: flatbuffers.Offset): void;
    static addX(builder: flatbuffers.Builder, x: number): void;
    static addY(builder: flatbuffers.Builder, y: number): void;
    static addW(builder: flatbuffers.Builder, w: number): void;
    static addH(builder: flatbuffers.Builder, h: number): void;
    static endClassificationCandidate(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createClassificationCandidate(builder: flatbuffers.Builder, confidence: number, classId: number, textOffset: flatbuffers.Offset, x: number, y: number, w: number, h: number): flatbuffers.Offset;
    unpack(): ClassificationCandidateT;
    unpackTo(_o: ClassificationCandidateT): void;
}
export declare class ClassificationCandidateT implements flatbuffers.IGeneratedObject {
    confidence: number;
    classId: number;
    text: string | Uint8Array | null;
    x: number;
    y: number;
    w: number;
    h: number;
    constructor(confidence?: number, classId?: number, text?: string | Uint8Array | null, x?: number, y?: number, w?: number, h?: number);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
