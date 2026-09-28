/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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
export declare class WirePayload implements flatbuffers.IUnpackableObject<WirePayloadT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): WirePayload;
    static getRootAsWirePayload(bb: flatbuffers.ByteBuffer, obj?: WirePayload): WirePayload;
    static getSizePrefixedRootAsWirePayload(bb: flatbuffers.ByteBuffer, obj?: WirePayload): WirePayload;
    id(): bigint;
    blob(index: number): number | null;
    blobLength(): number;
    blobArray(): Uint8Array | null;
    static startWirePayload(builder: flatbuffers.Builder): void;
    static addId(builder: flatbuffers.Builder, id: bigint): void;
    static addBlob(builder: flatbuffers.Builder, blobOffset: flatbuffers.Offset): void;
    static createBlobVector(builder: flatbuffers.Builder, data: number[] | Uint8Array): flatbuffers.Offset;
    static startBlobVector(builder: flatbuffers.Builder, numElems: number): void;
    static endWirePayload(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createWirePayload(builder: flatbuffers.Builder, id: bigint, blobOffset: flatbuffers.Offset): flatbuffers.Offset;
    unpack(): WirePayloadT;
    unpackTo(_o: WirePayloadT): void;
}
export declare class WirePayloadT implements flatbuffers.IGeneratedObject {
    id: bigint;
    blob: (number)[];
    constructor(id?: bigint, blob?: (number)[]);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
