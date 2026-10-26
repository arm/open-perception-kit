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
import { TurnDetection, TurnDetectionT } from '../../open-perception-kit/metadata/turn-detection.js';
export declare class TurnDetections implements flatbuffers.IUnpackableObject<TurnDetectionsT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): TurnDetections;
    static getRootAsTurnDetections(bb: flatbuffers.ByteBuffer, obj?: TurnDetections): TurnDetections;
    static getSizePrefixedRootAsTurnDetections(bb: flatbuffers.ByteBuffer, obj?: TurnDetections): TurnDetections;
    static bufferHasIdentifier(bb: flatbuffers.ByteBuffer): boolean;
    schemaMajor(): number;
    schemaMinor(): number;
    layer(obj?: LayerInfo): LayerInfo | null;
    detections(index: number, obj?: TurnDetection): TurnDetection | null;
    detectionsLength(): number;
    static startTurnDetections(builder: flatbuffers.Builder): void;
    static addSchemaMajor(builder: flatbuffers.Builder, schemaMajor: number): void;
    static addSchemaMinor(builder: flatbuffers.Builder, schemaMinor: number): void;
    static addLayer(builder: flatbuffers.Builder, layerOffset: flatbuffers.Offset): void;
    static addDetections(builder: flatbuffers.Builder, detectionsOffset: flatbuffers.Offset): void;
    static createDetectionsVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startDetectionsVector(builder: flatbuffers.Builder, numElems: number): void;
    static endTurnDetections(builder: flatbuffers.Builder): flatbuffers.Offset;
    static finishTurnDetectionsBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    static finishSizePrefixedTurnDetectionsBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    unpack(): TurnDetectionsT;
    unpackTo(_o: TurnDetectionsT): void;
}
export declare class TurnDetectionsT implements flatbuffers.IGeneratedObject {
    schemaMajor: number;
    schemaMinor: number;
    layer: LayerInfoT | null;
    detections: (TurnDetectionT)[];
    constructor(schemaMajor?: number, schemaMinor?: number, layer?: LayerInfoT | null, detections?: (TurnDetectionT)[]);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
