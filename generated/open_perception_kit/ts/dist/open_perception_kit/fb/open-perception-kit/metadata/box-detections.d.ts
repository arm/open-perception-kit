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
import { BoxDetection, BoxDetectionT } from '../../open-perception-kit/metadata/box-detection.js';
import { LayerInfo, LayerInfoT } from '../../open-perception-kit/metadata/layer-info.js';
export declare class BoxDetections implements flatbuffers.IUnpackableObject<BoxDetectionsT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): BoxDetections;
    static getRootAsBoxDetections(bb: flatbuffers.ByteBuffer, obj?: BoxDetections): BoxDetections;
    static getSizePrefixedRootAsBoxDetections(bb: flatbuffers.ByteBuffer, obj?: BoxDetections): BoxDetections;
    static bufferHasIdentifier(bb: flatbuffers.ByteBuffer): boolean;
    schemaMajor(): number;
    schemaMinor(): number;
    layer(obj?: LayerInfo): LayerInfo | null;
    detections(index: number, obj?: BoxDetection): BoxDetection | null;
    detectionsLength(): number;
    static startBoxDetections(builder: flatbuffers.Builder): void;
    static addSchemaMajor(builder: flatbuffers.Builder, schemaMajor: number): void;
    static addSchemaMinor(builder: flatbuffers.Builder, schemaMinor: number): void;
    static addLayer(builder: flatbuffers.Builder, layerOffset: flatbuffers.Offset): void;
    static addDetections(builder: flatbuffers.Builder, detectionsOffset: flatbuffers.Offset): void;
    static createDetectionsVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startDetectionsVector(builder: flatbuffers.Builder, numElems: number): void;
    static endBoxDetections(builder: flatbuffers.Builder): flatbuffers.Offset;
    static finishBoxDetectionsBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    static finishSizePrefixedBoxDetectionsBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    unpack(): BoxDetectionsT;
    unpackTo(_o: BoxDetectionsT): void;
}
export declare class BoxDetectionsT implements flatbuffers.IGeneratedObject {
    schemaMajor: number;
    schemaMinor: number;
    layer: LayerInfoT | null;
    detections: (BoxDetectionT)[];
    constructor(schemaMajor?: number, schemaMinor?: number, layer?: LayerInfoT | null, detections?: (BoxDetectionT)[]);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
