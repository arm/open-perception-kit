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
import { ObjectMeta, ObjectMetaT } from '../../open-perception-kit/metadata/object-meta.js';
export declare class SemanticSegmentationMask implements flatbuffers.IUnpackableObject<SemanticSegmentationMaskT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): SemanticSegmentationMask;
    static getRootAsSemanticSegmentationMask(bb: flatbuffers.ByteBuffer, obj?: SemanticSegmentationMask): SemanticSegmentationMask;
    static getSizePrefixedRootAsSemanticSegmentationMask(bb: flatbuffers.ByteBuffer, obj?: SemanticSegmentationMask): SemanticSegmentationMask;
    object(obj?: ObjectMeta): ObjectMeta | null;
    bitmap(obj?: BitmapData): BitmapData | null;
    static startSemanticSegmentationMask(builder: flatbuffers.Builder): void;
    static addObject(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset): void;
    static addBitmap(builder: flatbuffers.Builder, bitmapOffset: flatbuffers.Offset): void;
    static endSemanticSegmentationMask(builder: flatbuffers.Builder): flatbuffers.Offset;
    unpack(): SemanticSegmentationMaskT;
    unpackTo(_o: SemanticSegmentationMaskT): void;
}
export declare class SemanticSegmentationMaskT implements flatbuffers.IGeneratedObject {
    object: ObjectMetaT | null;
    bitmap: BitmapDataT | null;
    constructor(object?: ObjectMetaT | null, bitmap?: BitmapDataT | null);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
