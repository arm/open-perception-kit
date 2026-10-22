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
import { BitmapData, BitmapDataT } from '../../open-perception-kit/metadata/bitmap-data.js';
import { BoundingBox, BoundingBoxT } from '../../open-perception-kit/metadata/bounding-box.js';
import { ObjectMeta, ObjectMetaT } from '../../open-perception-kit/metadata/object-meta.js';
export declare class InstanceSegmentationMask implements flatbuffers.IUnpackableObject<InstanceSegmentationMaskT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): InstanceSegmentationMask;
    static getRootAsInstanceSegmentationMask(bb: flatbuffers.ByteBuffer, obj?: InstanceSegmentationMask): InstanceSegmentationMask;
    static getSizePrefixedRootAsInstanceSegmentationMask(bb: flatbuffers.ByteBuffer, obj?: InstanceSegmentationMask): InstanceSegmentationMask;
    object(obj?: ObjectMeta): ObjectMeta | null;
    bitmap(obj?: BitmapData): BitmapData | null;
    classId(): number;
    confidence(): number;
    box(obj?: BoundingBox): BoundingBox | null;
    static startInstanceSegmentationMask(builder: flatbuffers.Builder): void;
    static addObject(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset): void;
    static addBitmap(builder: flatbuffers.Builder, bitmapOffset: flatbuffers.Offset): void;
    static addClassId(builder: flatbuffers.Builder, classId: number): void;
    static addConfidence(builder: flatbuffers.Builder, confidence: number): void;
    static addBox(builder: flatbuffers.Builder, boxOffset: flatbuffers.Offset): void;
    static endInstanceSegmentationMask(builder: flatbuffers.Builder): flatbuffers.Offset;
    unpack(): InstanceSegmentationMaskT;
    unpackTo(_o: InstanceSegmentationMaskT): void;
}
export declare class InstanceSegmentationMaskT implements flatbuffers.IGeneratedObject {
    object: ObjectMetaT | null;
    bitmap: BitmapDataT | null;
    classId: number;
    confidence: number;
    box: BoundingBoxT | null;
    constructor(object?: ObjectMetaT | null, bitmap?: BitmapDataT | null, classId?: number, confidence?: number, box?: BoundingBoxT | null);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
