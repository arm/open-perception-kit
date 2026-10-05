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
import { TrackTrace, TrackTraceT } from '../../open-perception-kit/metadata/track-trace.js';
export declare class TrackTraces implements flatbuffers.IUnpackableObject<TrackTracesT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): TrackTraces;
    static getRootAsTrackTraces(bb: flatbuffers.ByteBuffer, obj?: TrackTraces): TrackTraces;
    static getSizePrefixedRootAsTrackTraces(bb: flatbuffers.ByteBuffer, obj?: TrackTraces): TrackTraces;
    static bufferHasIdentifier(bb: flatbuffers.ByteBuffer): boolean;
    schemaMajor(): number;
    schemaMinor(): number;
    layer(obj?: LayerInfo): LayerInfo | null;
    traces(index: number, obj?: TrackTrace): TrackTrace | null;
    tracesLength(): number;
    static startTrackTraces(builder: flatbuffers.Builder): void;
    static addSchemaMajor(builder: flatbuffers.Builder, schemaMajor: number): void;
    static addSchemaMinor(builder: flatbuffers.Builder, schemaMinor: number): void;
    static addLayer(builder: flatbuffers.Builder, layerOffset: flatbuffers.Offset): void;
    static addTraces(builder: flatbuffers.Builder, tracesOffset: flatbuffers.Offset): void;
    static createTracesVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startTracesVector(builder: flatbuffers.Builder, numElems: number): void;
    static endTrackTraces(builder: flatbuffers.Builder): flatbuffers.Offset;
    static finishTrackTracesBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    static finishSizePrefixedTrackTracesBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    unpack(): TrackTracesT;
    unpackTo(_o: TrackTracesT): void;
}
export declare class TrackTracesT implements flatbuffers.IGeneratedObject {
    schemaMajor: number;
    schemaMinor: number;
    layer: LayerInfoT | null;
    traces: (TrackTraceT)[];
    constructor(schemaMajor?: number, schemaMinor?: number, layer?: LayerInfoT | null, traces?: (TrackTraceT)[]);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
