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
import { FrameRate, FrameRateT } from '../../open-perception-kit/metadata/frame-rate.js';
import { ProducerInfo, ProducerInfoT } from '../../open-perception-kit/metadata/producer-info.js';
import { ScopeTiming, ScopeTimingT } from '../../open-perception-kit/metadata/scope-timing.js';
export declare class PerformanceMeasurements implements flatbuffers.IUnpackableObject<PerformanceMeasurementsT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): PerformanceMeasurements;
    static getRootAsPerformanceMeasurements(bb: flatbuffers.ByteBuffer, obj?: PerformanceMeasurements): PerformanceMeasurements;
    static getSizePrefixedRootAsPerformanceMeasurements(bb: flatbuffers.ByteBuffer, obj?: PerformanceMeasurements): PerformanceMeasurements;
    static bufferHasIdentifier(bb: flatbuffers.ByteBuffer): boolean;
    schemaMajor(): number;
    schemaMinor(): number;
    pixelFormat(): string | null;
    pixelFormat(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    timings(index: number, obj?: ScopeTiming): ScopeTiming | null;
    timingsLength(): number;
    frameRate(obj?: FrameRate): FrameRate | null;
    producer(obj?: ProducerInfo): ProducerInfo | null;
    static startPerformanceMeasurements(builder: flatbuffers.Builder): void;
    static addSchemaMajor(builder: flatbuffers.Builder, schemaMajor: number): void;
    static addSchemaMinor(builder: flatbuffers.Builder, schemaMinor: number): void;
    static addPixelFormat(builder: flatbuffers.Builder, pixelFormatOffset: flatbuffers.Offset): void;
    static addTimings(builder: flatbuffers.Builder, timingsOffset: flatbuffers.Offset): void;
    static createTimingsVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startTimingsVector(builder: flatbuffers.Builder, numElems: number): void;
    static addFrameRate(builder: flatbuffers.Builder, frameRateOffset: flatbuffers.Offset): void;
    static addProducer(builder: flatbuffers.Builder, producerOffset: flatbuffers.Offset): void;
    static endPerformanceMeasurements(builder: flatbuffers.Builder): flatbuffers.Offset;
    static finishPerformanceMeasurementsBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    static finishSizePrefixedPerformanceMeasurementsBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    unpack(): PerformanceMeasurementsT;
    unpackTo(_o: PerformanceMeasurementsT): void;
}
export declare class PerformanceMeasurementsT implements flatbuffers.IGeneratedObject {
    schemaMajor: number;
    schemaMinor: number;
    pixelFormat: string | Uint8Array | null;
    timings: (ScopeTimingT)[];
    frameRate: FrameRateT | null;
    producer: ProducerInfoT | null;
    constructor(schemaMajor?: number, schemaMinor?: number, pixelFormat?: string | Uint8Array | null, timings?: (ScopeTimingT)[], frameRate?: FrameRateT | null, producer?: ProducerInfoT | null);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
