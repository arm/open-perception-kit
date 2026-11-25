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
import { VisualPromptBox, VisualPromptBoxT } from '../../open-perception-kit/metadata/visual-prompt-box.js';
export declare class VisualPromptGroup implements flatbuffers.IUnpackableObject<VisualPromptGroupT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): VisualPromptGroup;
    static getRootAsVisualPromptGroup(bb: flatbuffers.ByteBuffer, obj?: VisualPromptGroup): VisualPromptGroup;
    static getSizePrefixedRootAsVisualPromptGroup(bb: flatbuffers.ByteBuffer, obj?: VisualPromptGroup): VisualPromptGroup;
    groupId(): string | null;
    groupId(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    displayName(): string | null;
    displayName(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    referenceIndex(): number;
    box(obj?: VisualPromptBox): VisualPromptBox | null;
    static startVisualPromptGroup(builder: flatbuffers.Builder): void;
    static addGroupId(builder: flatbuffers.Builder, groupIdOffset: flatbuffers.Offset): void;
    static addDisplayName(builder: flatbuffers.Builder, displayNameOffset: flatbuffers.Offset): void;
    static addReferenceIndex(builder: flatbuffers.Builder, referenceIndex: number): void;
    static addBox(builder: flatbuffers.Builder, boxOffset: flatbuffers.Offset): void;
    static endVisualPromptGroup(builder: flatbuffers.Builder): flatbuffers.Offset;
    unpack(): VisualPromptGroupT;
    unpackTo(_o: VisualPromptGroupT): void;
}
export declare class VisualPromptGroupT implements flatbuffers.IGeneratedObject {
    groupId: string | Uint8Array | null;
    displayName: string | Uint8Array | null;
    referenceIndex: number;
    box: VisualPromptBoxT | null;
    constructor(groupId?: string | Uint8Array | null, displayName?: string | Uint8Array | null, referenceIndex?: number, box?: VisualPromptBoxT | null);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
