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
import { LayerInfo, LayerInfoT } from '../../open-perception-kit/metadata/layer-info.js';
import { SegmentationMask, SegmentationMaskT } from '../../open-perception-kit/metadata/segmentation-mask.js';
export declare class SegmentationMasks implements flatbuffers.IUnpackableObject<SegmentationMasksT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): SegmentationMasks;
    static getRootAsSegmentationMasks(bb: flatbuffers.ByteBuffer, obj?: SegmentationMasks): SegmentationMasks;
    static getSizePrefixedRootAsSegmentationMasks(bb: flatbuffers.ByteBuffer, obj?: SegmentationMasks): SegmentationMasks;
    static bufferHasIdentifier(bb: flatbuffers.ByteBuffer): boolean;
    schemaMajor(): number;
    schemaMinor(): number;
    layer(obj?: LayerInfo): LayerInfo | null;
    masks(index: number, obj?: SegmentationMask): SegmentationMask | null;
    masksLength(): number;
    static startSegmentationMasks(builder: flatbuffers.Builder): void;
    static addSchemaMajor(builder: flatbuffers.Builder, schemaMajor: number): void;
    static addSchemaMinor(builder: flatbuffers.Builder, schemaMinor: number): void;
    static addLayer(builder: flatbuffers.Builder, layerOffset: flatbuffers.Offset): void;
    static addMasks(builder: flatbuffers.Builder, masksOffset: flatbuffers.Offset): void;
    static createMasksVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startMasksVector(builder: flatbuffers.Builder, numElems: number): void;
    static endSegmentationMasks(builder: flatbuffers.Builder): flatbuffers.Offset;
    static finishSegmentationMasksBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    static finishSizePrefixedSegmentationMasksBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    unpack(): SegmentationMasksT;
    unpackTo(_o: SegmentationMasksT): void;
}
export declare class SegmentationMasksT implements flatbuffers.IGeneratedObject {
    schemaMajor: number;
    schemaMinor: number;
    layer: LayerInfoT | null;
    masks: (SegmentationMaskT)[];
    constructor(schemaMajor?: number, schemaMinor?: number, layer?: LayerInfoT | null, masks?: (SegmentationMaskT)[]);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
