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
export declare class TurnDetection implements flatbuffers.IUnpackableObject<TurnDetectionT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): TurnDetection;
    static getRootAsTurnDetection(bb: flatbuffers.ByteBuffer, obj?: TurnDetection): TurnDetection;
    static getSizePrefixedRootAsTurnDetection(bb: flatbuffers.ByteBuffer, obj?: TurnDetection): TurnDetection;
    object(obj?: ObjectMeta): ObjectMeta | null;
    turnProbability(): number;
    turnComplete(): boolean;
    windowStartSample(): bigint;
    windowEndSample(): bigint;
    windowStartTsNs(): bigint | null;
    windowEndTsNs(): bigint | null;
    discontinuity(): boolean;
    static startTurnDetection(builder: flatbuffers.Builder): void;
    static addObject(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset): void;
    static addTurnProbability(builder: flatbuffers.Builder, turnProbability: number): void;
    static addTurnComplete(builder: flatbuffers.Builder, turnComplete: boolean): void;
    static addWindowStartSample(builder: flatbuffers.Builder, windowStartSample: bigint): void;
    static addWindowEndSample(builder: flatbuffers.Builder, windowEndSample: bigint): void;
    static addWindowStartTsNs(builder: flatbuffers.Builder, windowStartTsNs: bigint): void;
    static addWindowEndTsNs(builder: flatbuffers.Builder, windowEndTsNs: bigint): void;
    static addDiscontinuity(builder: flatbuffers.Builder, discontinuity: boolean): void;
    static endTurnDetection(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createTurnDetection(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset, turnProbability: number, turnComplete: boolean, windowStartSample: bigint, windowEndSample: bigint, windowStartTsNs: bigint | null, windowEndTsNs: bigint | null, discontinuity: boolean): flatbuffers.Offset;
    unpack(): TurnDetectionT;
    unpackTo(_o: TurnDetectionT): void;
}
export declare class TurnDetectionT implements flatbuffers.IGeneratedObject {
    object: ObjectMetaT | null;
    turnProbability: number;
    turnComplete: boolean;
    windowStartSample: bigint;
    windowEndSample: bigint;
    windowStartTsNs: bigint | null;
    windowEndTsNs: bigint | null;
    discontinuity: boolean;
    constructor(object?: ObjectMetaT | null, turnProbability?: number, turnComplete?: boolean, windowStartSample?: bigint, windowEndSample?: bigint, windowStartTsNs?: bigint | null, windowEndTsNs?: bigint | null, discontinuity?: boolean);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
