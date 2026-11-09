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
export declare class TrackIdentity implements flatbuffers.IUnpackableObject<TrackIdentityT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): TrackIdentity;
    static getRootAsTrackIdentity(bb: flatbuffers.ByteBuffer, obj?: TrackIdentity): TrackIdentity;
    static getSizePrefixedRootAsTrackIdentity(bb: flatbuffers.ByteBuffer, obj?: TrackIdentity): TrackIdentity;
    trackerId(): string | null;
    trackerId(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    sessionId(): string | null;
    sessionId(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    trackId(): bigint;
    static startTrackIdentity(builder: flatbuffers.Builder): void;
    static addTrackerId(builder: flatbuffers.Builder, trackerIdOffset: flatbuffers.Offset): void;
    static addSessionId(builder: flatbuffers.Builder, sessionIdOffset: flatbuffers.Offset): void;
    static addTrackId(builder: flatbuffers.Builder, trackId: bigint): void;
    static endTrackIdentity(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createTrackIdentity(builder: flatbuffers.Builder, trackerIdOffset: flatbuffers.Offset, sessionIdOffset: flatbuffers.Offset, trackId: bigint): flatbuffers.Offset;
    unpack(): TrackIdentityT;
    unpackTo(_o: TrackIdentityT): void;
}
export declare class TrackIdentityT implements flatbuffers.IGeneratedObject {
    trackerId: string | Uint8Array | null;
    sessionId: string | Uint8Array | null;
    trackId: bigint;
    constructor(trackerId?: string | Uint8Array | null, sessionId?: string | Uint8Array | null, trackId?: bigint);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
