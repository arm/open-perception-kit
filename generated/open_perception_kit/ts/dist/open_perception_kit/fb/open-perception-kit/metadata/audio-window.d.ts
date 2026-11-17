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
export declare class AudioWindow implements flatbuffers.IUnpackableObject<AudioWindowT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): AudioWindow;
    static getRootAsAudioWindow(bb: flatbuffers.ByteBuffer, obj?: AudioWindow): AudioWindow;
    static getSizePrefixedRootAsAudioWindow(bb: flatbuffers.ByteBuffer, obj?: AudioWindow): AudioWindow;
    sourceId(): string | null;
    sourceId(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    sessionId(): string | null;
    sessionId(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    segmentIndex(): bigint;
    sampleRateHz(): number;
    windowStartSample(): bigint;
    windowEndSample(): bigint;
    windowStartTsNs(): bigint | null;
    windowEndTsNs(): bigint | null;
    discontinuity(): boolean;
    static startAudioWindow(builder: flatbuffers.Builder): void;
    static addSourceId(builder: flatbuffers.Builder, sourceIdOffset: flatbuffers.Offset): void;
    static addSessionId(builder: flatbuffers.Builder, sessionIdOffset: flatbuffers.Offset): void;
    static addSegmentIndex(builder: flatbuffers.Builder, segmentIndex: bigint): void;
    static addSampleRateHz(builder: flatbuffers.Builder, sampleRateHz: number): void;
    static addWindowStartSample(builder: flatbuffers.Builder, windowStartSample: bigint): void;
    static addWindowEndSample(builder: flatbuffers.Builder, windowEndSample: bigint): void;
    static addWindowStartTsNs(builder: flatbuffers.Builder, windowStartTsNs: bigint): void;
    static addWindowEndTsNs(builder: flatbuffers.Builder, windowEndTsNs: bigint): void;
    static addDiscontinuity(builder: flatbuffers.Builder, discontinuity: boolean): void;
    static endAudioWindow(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createAudioWindow(builder: flatbuffers.Builder, sourceIdOffset: flatbuffers.Offset, sessionIdOffset: flatbuffers.Offset, segmentIndex: bigint, sampleRateHz: number, windowStartSample: bigint, windowEndSample: bigint, windowStartTsNs: bigint | null, windowEndTsNs: bigint | null, discontinuity: boolean): flatbuffers.Offset;
    unpack(): AudioWindowT;
    unpackTo(_o: AudioWindowT): void;
}
export declare class AudioWindowT implements flatbuffers.IGeneratedObject {
    sourceId: string | Uint8Array | null;
    sessionId: string | Uint8Array | null;
    segmentIndex: bigint;
    sampleRateHz: number;
    windowStartSample: bigint;
    windowEndSample: bigint;
    windowStartTsNs: bigint | null;
    windowEndTsNs: bigint | null;
    discontinuity: boolean;
    constructor(sourceId?: string | Uint8Array | null, sessionId?: string | Uint8Array | null, segmentIndex?: bigint, sampleRateHz?: number, windowStartSample?: bigint, windowEndSample?: bigint, windowStartTsNs?: bigint | null, windowEndTsNs?: bigint | null, discontinuity?: boolean);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
