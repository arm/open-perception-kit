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
import { InstanceSegmentationMask, InstanceSegmentationMaskT } from '../../open-perception-kit/metadata/instance-segmentation-mask.js';
import { LayerInfo, LayerInfoT } from '../../open-perception-kit/metadata/layer-info.js';
export declare class InstanceSegmentationMasks implements flatbuffers.IUnpackableObject<InstanceSegmentationMasksT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): InstanceSegmentationMasks;
    static getRootAsInstanceSegmentationMasks(bb: flatbuffers.ByteBuffer, obj?: InstanceSegmentationMasks): InstanceSegmentationMasks;
    static getSizePrefixedRootAsInstanceSegmentationMasks(bb: flatbuffers.ByteBuffer, obj?: InstanceSegmentationMasks): InstanceSegmentationMasks;
    static bufferHasIdentifier(bb: flatbuffers.ByteBuffer): boolean;
    schemaMajor(): number;
    schemaMinor(): number;
    layer(obj?: LayerInfo): LayerInfo | null;
    masks(index: number, obj?: InstanceSegmentationMask): InstanceSegmentationMask | null;
    masksLength(): number;
    static startInstanceSegmentationMasks(builder: flatbuffers.Builder): void;
    static addSchemaMajor(builder: flatbuffers.Builder, schemaMajor: number): void;
    static addSchemaMinor(builder: flatbuffers.Builder, schemaMinor: number): void;
    static addLayer(builder: flatbuffers.Builder, layerOffset: flatbuffers.Offset): void;
    static addMasks(builder: flatbuffers.Builder, masksOffset: flatbuffers.Offset): void;
    static createMasksVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startMasksVector(builder: flatbuffers.Builder, numElems: number): void;
    static endInstanceSegmentationMasks(builder: flatbuffers.Builder): flatbuffers.Offset;
    static finishInstanceSegmentationMasksBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    static finishSizePrefixedInstanceSegmentationMasksBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    unpack(): InstanceSegmentationMasksT;
    unpackTo(_o: InstanceSegmentationMasksT): void;
}
export declare class InstanceSegmentationMasksT implements flatbuffers.IGeneratedObject {
    schemaMajor: number;
    schemaMinor: number;
    layer: LayerInfoT | null;
    masks: (InstanceSegmentationMaskT)[];
    constructor(schemaMajor?: number, schemaMinor?: number, layer?: LayerInfoT | null, masks?: (InstanceSegmentationMaskT)[]);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
