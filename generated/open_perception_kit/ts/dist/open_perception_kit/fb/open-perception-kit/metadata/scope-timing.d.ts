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
export declare class ScopeTiming implements flatbuffers.IUnpackableObject<ScopeTimingT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): ScopeTiming;
    static getRootAsScopeTiming(bb: flatbuffers.ByteBuffer, obj?: ScopeTiming): ScopeTiming;
    static getSizePrefixedRootAsScopeTiming(bb: flatbuffers.ByteBuffer, obj?: ScopeTiming): ScopeTiming;
    name(): string | null;
    name(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    meanMs(): number | null;
    p95Ms(): number | null;
    scopeId(): string | null;
    scopeId(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    static startScopeTiming(builder: flatbuffers.Builder): void;
    static addName(builder: flatbuffers.Builder, nameOffset: flatbuffers.Offset): void;
    static addMeanMs(builder: flatbuffers.Builder, meanMs: number): void;
    static addP95Ms(builder: flatbuffers.Builder, p95Ms: number): void;
    static addScopeId(builder: flatbuffers.Builder, scopeIdOffset: flatbuffers.Offset): void;
    static endScopeTiming(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createScopeTiming(builder: flatbuffers.Builder, nameOffset: flatbuffers.Offset, meanMs: number | null, p95Ms: number | null, scopeIdOffset: flatbuffers.Offset): flatbuffers.Offset;
    unpack(): ScopeTimingT;
    unpackTo(_o: ScopeTimingT): void;
}
export declare class ScopeTimingT implements flatbuffers.IGeneratedObject {
    name: string | Uint8Array | null;
    meanMs: number | null;
    p95Ms: number | null;
    scopeId: string | Uint8Array | null;
    constructor(name?: string | Uint8Array | null, meanMs?: number | null, p95Ms?: number | null, scopeId?: string | Uint8Array | null);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
