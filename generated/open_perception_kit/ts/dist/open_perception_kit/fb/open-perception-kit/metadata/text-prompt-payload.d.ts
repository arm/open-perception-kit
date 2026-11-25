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
import { TextPromptGroup, TextPromptGroupT } from '../../open-perception-kit/metadata/text-prompt-group.js';
export declare class TextPromptPayload implements flatbuffers.IUnpackableObject<TextPromptPayloadT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): TextPromptPayload;
    static getRootAsTextPromptPayload(bb: flatbuffers.ByteBuffer, obj?: TextPromptPayload): TextPromptPayload;
    static getSizePrefixedRootAsTextPromptPayload(bb: flatbuffers.ByteBuffer, obj?: TextPromptPayload): TextPromptPayload;
    groups(index: number, obj?: TextPromptGroup): TextPromptGroup | null;
    groupsLength(): number;
    static startTextPromptPayload(builder: flatbuffers.Builder): void;
    static addGroups(builder: flatbuffers.Builder, groupsOffset: flatbuffers.Offset): void;
    static createGroupsVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startGroupsVector(builder: flatbuffers.Builder, numElems: number): void;
    static endTextPromptPayload(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createTextPromptPayload(builder: flatbuffers.Builder, groupsOffset: flatbuffers.Offset): flatbuffers.Offset;
    unpack(): TextPromptPayloadT;
    unpackTo(_o: TextPromptPayloadT): void;
}
export declare class TextPromptPayloadT implements flatbuffers.IGeneratedObject {
    groups: (TextPromptGroupT)[];
    constructor(groups?: (TextPromptGroupT)[]);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
