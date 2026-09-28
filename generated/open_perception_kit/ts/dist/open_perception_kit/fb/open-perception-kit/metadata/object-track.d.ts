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
import { BoundingBox, BoundingBoxT } from '../../open-perception-kit/metadata/bounding-box.js';
import { ObjectMeta, ObjectMetaT } from '../../open-perception-kit/metadata/object-meta.js';
export declare class ObjectTrack implements flatbuffers.IUnpackableObject<ObjectTrackT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): ObjectTrack;
    static getRootAsObjectTrack(bb: flatbuffers.ByteBuffer, obj?: ObjectTrack): ObjectTrack;
    static getSizePrefixedRootAsObjectTrack(bb: flatbuffers.ByteBuffer, obj?: ObjectTrack): ObjectTrack;
    object(obj?: ObjectMeta): ObjectMeta | null;
    sourceId(): bigint;
    trackId(): bigint;
    box(obj?: BoundingBox): BoundingBox | null;
    confidence(): number;
    classId(): number;
    text(): string | null;
    text(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    diagnostic(): string | null;
    diagnostic(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    predictedOnly(): boolean;
    static startObjectTrack(builder: flatbuffers.Builder): void;
    static addObject(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset): void;
    static addSourceId(builder: flatbuffers.Builder, sourceId: bigint): void;
    static addTrackId(builder: flatbuffers.Builder, trackId: bigint): void;
    static addBox(builder: flatbuffers.Builder, boxOffset: flatbuffers.Offset): void;
    static addConfidence(builder: flatbuffers.Builder, confidence: number): void;
    static addClassId(builder: flatbuffers.Builder, classId: number): void;
    static addText(builder: flatbuffers.Builder, textOffset: flatbuffers.Offset): void;
    static addDiagnostic(builder: flatbuffers.Builder, diagnosticOffset: flatbuffers.Offset): void;
    static addPredictedOnly(builder: flatbuffers.Builder, predictedOnly: boolean): void;
    static endObjectTrack(builder: flatbuffers.Builder): flatbuffers.Offset;
    unpack(): ObjectTrackT;
    unpackTo(_o: ObjectTrackT): void;
}
export declare class ObjectTrackT implements flatbuffers.IGeneratedObject {
    object: ObjectMetaT | null;
    sourceId: bigint;
    trackId: bigint;
    box: BoundingBoxT | null;
    confidence: number;
    classId: number;
    text: string | Uint8Array | null;
    diagnostic: string | Uint8Array | null;
    predictedOnly: boolean;
    constructor(object?: ObjectMetaT | null, sourceId?: bigint, trackId?: bigint, box?: BoundingBoxT | null, confidence?: number, classId?: number, text?: string | Uint8Array | null, diagnostic?: string | Uint8Array | null, predictedOnly?: boolean);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
