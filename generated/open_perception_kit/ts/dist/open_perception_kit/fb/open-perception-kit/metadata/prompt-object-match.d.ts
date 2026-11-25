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
export declare class PromptObjectMatch implements flatbuffers.IUnpackableObject<PromptObjectMatchT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): PromptObjectMatch;
    static getRootAsPromptObjectMatch(bb: flatbuffers.ByteBuffer, obj?: PromptObjectMatch): PromptObjectMatch;
    static getSizePrefixedRootAsPromptObjectMatch(bb: flatbuffers.ByteBuffer, obj?: PromptObjectMatch): PromptObjectMatch;
    objectId(): bigint;
    groupId(): string | null;
    groupId(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    static startPromptObjectMatch(builder: flatbuffers.Builder): void;
    static addObjectId(builder: flatbuffers.Builder, objectId: bigint): void;
    static addGroupId(builder: flatbuffers.Builder, groupIdOffset: flatbuffers.Offset): void;
    static endPromptObjectMatch(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createPromptObjectMatch(builder: flatbuffers.Builder, objectId: bigint, groupIdOffset: flatbuffers.Offset): flatbuffers.Offset;
    unpack(): PromptObjectMatchT;
    unpackTo(_o: PromptObjectMatchT): void;
}
export declare class PromptObjectMatchT implements flatbuffers.IGeneratedObject {
    objectId: bigint;
    groupId: string | Uint8Array | null;
    constructor(objectId?: bigint, groupId?: string | Uint8Array | null);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
