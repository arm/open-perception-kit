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
import { GazeMeasurement, GazeMeasurementT } from '../../open-perception-kit/metadata/gaze-measurement.js';
import { LayerInfo, LayerInfoT } from '../../open-perception-kit/metadata/layer-info.js';
export declare class GazeMeasurements implements flatbuffers.IUnpackableObject<GazeMeasurementsT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): GazeMeasurements;
    static getRootAsGazeMeasurements(bb: flatbuffers.ByteBuffer, obj?: GazeMeasurements): GazeMeasurements;
    static getSizePrefixedRootAsGazeMeasurements(bb: flatbuffers.ByteBuffer, obj?: GazeMeasurements): GazeMeasurements;
    static bufferHasIdentifier(bb: flatbuffers.ByteBuffer): boolean;
    schemaMajor(): number;
    schemaMinor(): number;
    layer(obj?: LayerInfo): LayerInfo | null;
    measurements(index: number, obj?: GazeMeasurement): GazeMeasurement | null;
    measurementsLength(): number;
    static startGazeMeasurements(builder: flatbuffers.Builder): void;
    static addSchemaMajor(builder: flatbuffers.Builder, schemaMajor: number): void;
    static addSchemaMinor(builder: flatbuffers.Builder, schemaMinor: number): void;
    static addLayer(builder: flatbuffers.Builder, layerOffset: flatbuffers.Offset): void;
    static addMeasurements(builder: flatbuffers.Builder, measurementsOffset: flatbuffers.Offset): void;
    static createMeasurementsVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startMeasurementsVector(builder: flatbuffers.Builder, numElems: number): void;
    static endGazeMeasurements(builder: flatbuffers.Builder): flatbuffers.Offset;
    static finishGazeMeasurementsBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    static finishSizePrefixedGazeMeasurementsBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    unpack(): GazeMeasurementsT;
    unpackTo(_o: GazeMeasurementsT): void;
}
export declare class GazeMeasurementsT implements flatbuffers.IGeneratedObject {
    schemaMajor: number;
    schemaMinor: number;
    layer: LayerInfoT | null;
    measurements: (GazeMeasurementT)[];
    constructor(schemaMajor?: number, schemaMinor?: number, layer?: LayerInfoT | null, measurements?: (GazeMeasurementT)[]);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
