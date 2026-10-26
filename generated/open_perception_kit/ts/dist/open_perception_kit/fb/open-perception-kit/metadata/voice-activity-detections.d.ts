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
import { VoiceActivityDetection, VoiceActivityDetectionT } from '../../open-perception-kit/metadata/voice-activity-detection.js';
export declare class VoiceActivityDetections implements flatbuffers.IUnpackableObject<VoiceActivityDetectionsT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): VoiceActivityDetections;
    static getRootAsVoiceActivityDetections(bb: flatbuffers.ByteBuffer, obj?: VoiceActivityDetections): VoiceActivityDetections;
    static getSizePrefixedRootAsVoiceActivityDetections(bb: flatbuffers.ByteBuffer, obj?: VoiceActivityDetections): VoiceActivityDetections;
    static bufferHasIdentifier(bb: flatbuffers.ByteBuffer): boolean;
    schemaMajor(): number;
    schemaMinor(): number;
    layer(obj?: LayerInfo): LayerInfo | null;
    detections(index: number, obj?: VoiceActivityDetection): VoiceActivityDetection | null;
    detectionsLength(): number;
    static startVoiceActivityDetections(builder: flatbuffers.Builder): void;
    static addSchemaMajor(builder: flatbuffers.Builder, schemaMajor: number): void;
    static addSchemaMinor(builder: flatbuffers.Builder, schemaMinor: number): void;
    static addLayer(builder: flatbuffers.Builder, layerOffset: flatbuffers.Offset): void;
    static addDetections(builder: flatbuffers.Builder, detectionsOffset: flatbuffers.Offset): void;
    static createDetectionsVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startDetectionsVector(builder: flatbuffers.Builder, numElems: number): void;
    static endVoiceActivityDetections(builder: flatbuffers.Builder): flatbuffers.Offset;
    static finishVoiceActivityDetectionsBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    static finishSizePrefixedVoiceActivityDetectionsBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    unpack(): VoiceActivityDetectionsT;
    unpackTo(_o: VoiceActivityDetectionsT): void;
}
export declare class VoiceActivityDetectionsT implements flatbuffers.IGeneratedObject {
    schemaMajor: number;
    schemaMinor: number;
    layer: LayerInfoT | null;
    detections: (VoiceActivityDetectionT)[];
    constructor(schemaMajor?: number, schemaMinor?: number, layer?: LayerInfoT | null, detections?: (VoiceActivityDetectionT)[]);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
