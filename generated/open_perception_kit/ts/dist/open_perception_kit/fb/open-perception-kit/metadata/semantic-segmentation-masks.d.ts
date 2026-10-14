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
import { LayerInfo, LayerInfoT } from '../../open-perception-kit/metadata/layer-info.js';
import { SemanticSegmentationMask, SemanticSegmentationMaskT } from '../../open-perception-kit/metadata/semantic-segmentation-mask.js';
export declare class SemanticSegmentationMasks implements flatbuffers.IUnpackableObject<SemanticSegmentationMasksT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): SemanticSegmentationMasks;
    static getRootAsSemanticSegmentationMasks(bb: flatbuffers.ByteBuffer, obj?: SemanticSegmentationMasks): SemanticSegmentationMasks;
    static getSizePrefixedRootAsSemanticSegmentationMasks(bb: flatbuffers.ByteBuffer, obj?: SemanticSegmentationMasks): SemanticSegmentationMasks;
    static bufferHasIdentifier(bb: flatbuffers.ByteBuffer): boolean;
    schemaMajor(): number;
    schemaMinor(): number;
    layer(obj?: LayerInfo): LayerInfo | null;
    masks(index: number, obj?: SemanticSegmentationMask): SemanticSegmentationMask | null;
    masksLength(): number;
    static startSemanticSegmentationMasks(builder: flatbuffers.Builder): void;
    static addSchemaMajor(builder: flatbuffers.Builder, schemaMajor: number): void;
    static addSchemaMinor(builder: flatbuffers.Builder, schemaMinor: number): void;
    static addLayer(builder: flatbuffers.Builder, layerOffset: flatbuffers.Offset): void;
    static addMasks(builder: flatbuffers.Builder, masksOffset: flatbuffers.Offset): void;
    static createMasksVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startMasksVector(builder: flatbuffers.Builder, numElems: number): void;
    static endSemanticSegmentationMasks(builder: flatbuffers.Builder): flatbuffers.Offset;
    static finishSemanticSegmentationMasksBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    static finishSizePrefixedSemanticSegmentationMasksBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    unpack(): SemanticSegmentationMasksT;
    unpackTo(_o: SemanticSegmentationMasksT): void;
}
export declare class SemanticSegmentationMasksT implements flatbuffers.IGeneratedObject {
    schemaMajor: number;
    schemaMinor: number;
    layer: LayerInfoT | null;
    masks: (SemanticSegmentationMaskT)[];
    constructor(schemaMajor?: number, schemaMinor?: number, layer?: LayerInfoT | null, masks?: (SemanticSegmentationMaskT)[]);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
