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
export declare class TextPromptGroup implements flatbuffers.IUnpackableObject<TextPromptGroupT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): TextPromptGroup;
    static getRootAsTextPromptGroup(bb: flatbuffers.ByteBuffer, obj?: TextPromptGroup): TextPromptGroup;
    static getSizePrefixedRootAsTextPromptGroup(bb: flatbuffers.ByteBuffer, obj?: TextPromptGroup): TextPromptGroup;
    groupId(): string | null;
    groupId(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    description(): string | null;
    description(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    displayName(): string | null;
    displayName(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    static startTextPromptGroup(builder: flatbuffers.Builder): void;
    static addGroupId(builder: flatbuffers.Builder, groupIdOffset: flatbuffers.Offset): void;
    static addDescription(builder: flatbuffers.Builder, descriptionOffset: flatbuffers.Offset): void;
    static addDisplayName(builder: flatbuffers.Builder, displayNameOffset: flatbuffers.Offset): void;
    static endTextPromptGroup(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createTextPromptGroup(builder: flatbuffers.Builder, groupIdOffset: flatbuffers.Offset, descriptionOffset: flatbuffers.Offset, displayNameOffset: flatbuffers.Offset): flatbuffers.Offset;
    unpack(): TextPromptGroupT;
    unpackTo(_o: TextPromptGroupT): void;
}
export declare class TextPromptGroupT implements flatbuffers.IGeneratedObject {
    groupId: string | Uint8Array | null;
    description: string | Uint8Array | null;
    displayName: string | Uint8Array | null;
    constructor(groupId?: string | Uint8Array | null, description?: string | Uint8Array | null, displayName?: string | Uint8Array | null);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
