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
export declare class AudioFrameContext implements flatbuffers.IUnpackableObject<AudioFrameContextT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): AudioFrameContext;
    static getRootAsAudioFrameContext(bb: flatbuffers.ByteBuffer, obj?: AudioFrameContext): AudioFrameContext;
    static getSizePrefixedRootAsAudioFrameContext(bb: flatbuffers.ByteBuffer, obj?: AudioFrameContext): AudioFrameContext;
    object(obj?: ObjectMeta): ObjectMeta | null;
    originalChannels(): bigint;
    originalFrequency(): bigint;
    originalSampleCount(): bigint;
    cutLeftSampleCount(): bigint;
    cutRightSampleCount(): bigint;
    static startAudioFrameContext(builder: flatbuffers.Builder): void;
    static addObject(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset): void;
    static addOriginalChannels(builder: flatbuffers.Builder, originalChannels: bigint): void;
    static addOriginalFrequency(builder: flatbuffers.Builder, originalFrequency: bigint): void;
    static addOriginalSampleCount(builder: flatbuffers.Builder, originalSampleCount: bigint): void;
    static addCutLeftSampleCount(builder: flatbuffers.Builder, cutLeftSampleCount: bigint): void;
    static addCutRightSampleCount(builder: flatbuffers.Builder, cutRightSampleCount: bigint): void;
    static endAudioFrameContext(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createAudioFrameContext(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset, originalChannels: bigint, originalFrequency: bigint, originalSampleCount: bigint, cutLeftSampleCount: bigint, cutRightSampleCount: bigint): flatbuffers.Offset;
    unpack(): AudioFrameContextT;
    unpackTo(_o: AudioFrameContextT): void;
}
export declare class AudioFrameContextT implements flatbuffers.IGeneratedObject {
    object: ObjectMetaT | null;
    originalChannels: bigint;
    originalFrequency: bigint;
    originalSampleCount: bigint;
    cutLeftSampleCount: bigint;
    cutRightSampleCount: bigint;
    constructor(object?: ObjectMetaT | null, originalChannels?: bigint, originalFrequency?: bigint, originalSampleCount?: bigint, cutLeftSampleCount?: bigint, cutRightSampleCount?: bigint);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
