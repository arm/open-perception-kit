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
export declare class VoiceActivityDetection implements flatbuffers.IUnpackableObject<VoiceActivityDetectionT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): VoiceActivityDetection;
    static getRootAsVoiceActivityDetection(bb: flatbuffers.ByteBuffer, obj?: VoiceActivityDetection): VoiceActivityDetection;
    static getSizePrefixedRootAsVoiceActivityDetection(bb: flatbuffers.ByteBuffer, obj?: VoiceActivityDetection): VoiceActivityDetection;
    object(obj?: ObjectMeta): ObjectMeta | null;
    speechProbability(): number;
    speechActive(): boolean;
    windowStartSample(): bigint;
    windowEndSample(): bigint;
    windowStartTsNs(): bigint | null;
    windowEndTsNs(): bigint | null;
    discontinuity(): boolean;
    static startVoiceActivityDetection(builder: flatbuffers.Builder): void;
    static addObject(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset): void;
    static addSpeechProbability(builder: flatbuffers.Builder, speechProbability: number): void;
    static addSpeechActive(builder: flatbuffers.Builder, speechActive: boolean): void;
    static addWindowStartSample(builder: flatbuffers.Builder, windowStartSample: bigint): void;
    static addWindowEndSample(builder: flatbuffers.Builder, windowEndSample: bigint): void;
    static addWindowStartTsNs(builder: flatbuffers.Builder, windowStartTsNs: bigint): void;
    static addWindowEndTsNs(builder: flatbuffers.Builder, windowEndTsNs: bigint): void;
    static addDiscontinuity(builder: flatbuffers.Builder, discontinuity: boolean): void;
    static endVoiceActivityDetection(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createVoiceActivityDetection(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset, speechProbability: number, speechActive: boolean, windowStartSample: bigint, windowEndSample: bigint, windowStartTsNs: bigint | null, windowEndTsNs: bigint | null, discontinuity: boolean): flatbuffers.Offset;
    unpack(): VoiceActivityDetectionT;
    unpackTo(_o: VoiceActivityDetectionT): void;
}
export declare class VoiceActivityDetectionT implements flatbuffers.IGeneratedObject {
    object: ObjectMetaT | null;
    speechProbability: number;
    speechActive: boolean;
    windowStartSample: bigint;
    windowEndSample: bigint;
    windowStartTsNs: bigint | null;
    windowEndTsNs: bigint | null;
    discontinuity: boolean;
    constructor(object?: ObjectMetaT | null, speechProbability?: number, speechActive?: boolean, windowStartSample?: bigint, windowEndSample?: bigint, windowStartTsNs?: bigint | null, windowEndTsNs?: bigint | null, discontinuity?: boolean);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
