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
import { ObjectMeta, ObjectMetaT } from '../../open-perception-kit/metadata/object-meta.js';
import { OrientedBoundingBox, OrientedBoundingBoxT } from '../../open-perception-kit/metadata/oriented-bounding-box.js';
export declare class OrientedBoxDetection implements flatbuffers.IUnpackableObject<OrientedBoxDetectionT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): OrientedBoxDetection;
    static getRootAsOrientedBoxDetection(bb: flatbuffers.ByteBuffer, obj?: OrientedBoxDetection): OrientedBoxDetection;
    static getSizePrefixedRootAsOrientedBoxDetection(bb: flatbuffers.ByteBuffer, obj?: OrientedBoxDetection): OrientedBoxDetection;
    object(obj?: ObjectMeta): ObjectMeta | null;
    box(obj?: OrientedBoundingBox): OrientedBoundingBox | null;
    confidence(): number;
    classId(): number;
    text(): string | null;
    text(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    static startOrientedBoxDetection(builder: flatbuffers.Builder): void;
    static addObject(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset): void;
    static addBox(builder: flatbuffers.Builder, boxOffset: flatbuffers.Offset): void;
    static addConfidence(builder: flatbuffers.Builder, confidence: number): void;
    static addClassId(builder: flatbuffers.Builder, classId: number): void;
    static addText(builder: flatbuffers.Builder, textOffset: flatbuffers.Offset): void;
    static endOrientedBoxDetection(builder: flatbuffers.Builder): flatbuffers.Offset;
    unpack(): OrientedBoxDetectionT;
    unpackTo(_o: OrientedBoxDetectionT): void;
}
export declare class OrientedBoxDetectionT implements flatbuffers.IGeneratedObject {
    object: ObjectMetaT | null;
    box: OrientedBoundingBoxT | null;
    confidence: number;
    classId: number;
    text: string | Uint8Array | null;
    constructor(object?: ObjectMetaT | null, box?: OrientedBoundingBoxT | null, confidence?: number, classId?: number, text?: string | Uint8Array | null);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
