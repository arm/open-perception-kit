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
import { PitchEstimate, PitchEstimateT } from '../../open-perception-kit/metadata/pitch-estimate.js';
export declare class PitchEstimates implements flatbuffers.IUnpackableObject<PitchEstimatesT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): PitchEstimates;
    static getRootAsPitchEstimates(bb: flatbuffers.ByteBuffer, obj?: PitchEstimates): PitchEstimates;
    static getSizePrefixedRootAsPitchEstimates(bb: flatbuffers.ByteBuffer, obj?: PitchEstimates): PitchEstimates;
    static bufferHasIdentifier(bb: flatbuffers.ByteBuffer): boolean;
    schemaMajor(): number;
    schemaMinor(): number;
    layer(obj?: LayerInfo): LayerInfo | null;
    estimates(index: number, obj?: PitchEstimate): PitchEstimate | null;
    estimatesLength(): number;
    static startPitchEstimates(builder: flatbuffers.Builder): void;
    static addSchemaMajor(builder: flatbuffers.Builder, schemaMajor: number): void;
    static addSchemaMinor(builder: flatbuffers.Builder, schemaMinor: number): void;
    static addLayer(builder: flatbuffers.Builder, layerOffset: flatbuffers.Offset): void;
    static addEstimates(builder: flatbuffers.Builder, estimatesOffset: flatbuffers.Offset): void;
    static createEstimatesVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startEstimatesVector(builder: flatbuffers.Builder, numElems: number): void;
    static endPitchEstimates(builder: flatbuffers.Builder): flatbuffers.Offset;
    static finishPitchEstimatesBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    static finishSizePrefixedPitchEstimatesBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    unpack(): PitchEstimatesT;
    unpackTo(_o: PitchEstimatesT): void;
}
export declare class PitchEstimatesT implements flatbuffers.IGeneratedObject {
    schemaMajor: number;
    schemaMinor: number;
    layer: LayerInfoT | null;
    estimates: (PitchEstimateT)[];
    constructor(schemaMajor?: number, schemaMinor?: number, layer?: LayerInfoT | null, estimates?: (PitchEstimateT)[]);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
