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
import { ObjectMeta, ObjectMetaT } from '../../open-perception-kit/metadata/object-meta.js';
export declare class PitchEstimate implements flatbuffers.IUnpackableObject<PitchEstimateT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): PitchEstimate;
    static getRootAsPitchEstimate(bb: flatbuffers.ByteBuffer, obj?: PitchEstimate): PitchEstimate;
    static getSizePrefixedRootAsPitchEstimate(bb: flatbuffers.ByteBuffer, obj?: PitchEstimate): PitchEstimate;
    object(obj?: ObjectMeta): ObjectMeta | null;
    frequencyHz(): number | null;
    periodicity(): number | null;
    voiced(): boolean | null;
    windowStartSample(): bigint;
    windowEndSample(): bigint;
    windowStartTsNs(): bigint | null;
    windowEndTsNs(): bigint | null;
    discontinuity(): boolean;
    static startPitchEstimate(builder: flatbuffers.Builder): void;
    static addObject(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset): void;
    static addFrequencyHz(builder: flatbuffers.Builder, frequencyHz: number): void;
    static addPeriodicity(builder: flatbuffers.Builder, periodicity: number): void;
    static addVoiced(builder: flatbuffers.Builder, voiced: boolean): void;
    static addWindowStartSample(builder: flatbuffers.Builder, windowStartSample: bigint): void;
    static addWindowEndSample(builder: flatbuffers.Builder, windowEndSample: bigint): void;
    static addWindowStartTsNs(builder: flatbuffers.Builder, windowStartTsNs: bigint): void;
    static addWindowEndTsNs(builder: flatbuffers.Builder, windowEndTsNs: bigint): void;
    static addDiscontinuity(builder: flatbuffers.Builder, discontinuity: boolean): void;
    static endPitchEstimate(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createPitchEstimate(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset, frequencyHz: number | null, periodicity: number | null, voiced: boolean | null, windowStartSample: bigint, windowEndSample: bigint, windowStartTsNs: bigint | null, windowEndTsNs: bigint | null, discontinuity: boolean): flatbuffers.Offset;
    unpack(): PitchEstimateT;
    unpackTo(_o: PitchEstimateT): void;
}
export declare class PitchEstimateT implements flatbuffers.IGeneratedObject {
    object: ObjectMetaT | null;
    frequencyHz: number | null;
    periodicity: number | null;
    voiced: boolean | null;
    windowStartSample: bigint;
    windowEndSample: bigint;
    windowStartTsNs: bigint | null;
    windowEndTsNs: bigint | null;
    discontinuity: boolean;
    constructor(object?: ObjectMetaT | null, frequencyHz?: number | null, periodicity?: number | null, voiced?: boolean | null, windowStartSample?: bigint, windowEndSample?: bigint, windowStartTsNs?: bigint | null, windowEndTsNs?: bigint | null, discontinuity?: boolean);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
