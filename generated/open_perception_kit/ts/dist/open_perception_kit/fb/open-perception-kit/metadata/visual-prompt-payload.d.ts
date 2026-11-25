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
import { PromptReferenceImage, PromptReferenceImageT } from '../../open-perception-kit/metadata/prompt-reference-image.js';
import { VisualPromptGroup, VisualPromptGroupT } from '../../open-perception-kit/metadata/visual-prompt-group.js';
export declare class VisualPromptPayload implements flatbuffers.IUnpackableObject<VisualPromptPayloadT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): VisualPromptPayload;
    static getRootAsVisualPromptPayload(bb: flatbuffers.ByteBuffer, obj?: VisualPromptPayload): VisualPromptPayload;
    static getSizePrefixedRootAsVisualPromptPayload(bb: flatbuffers.ByteBuffer, obj?: VisualPromptPayload): VisualPromptPayload;
    references(index: number, obj?: PromptReferenceImage): PromptReferenceImage | null;
    referencesLength(): number;
    groups(index: number, obj?: VisualPromptGroup): VisualPromptGroup | null;
    groupsLength(): number;
    static startVisualPromptPayload(builder: flatbuffers.Builder): void;
    static addReferences(builder: flatbuffers.Builder, referencesOffset: flatbuffers.Offset): void;
    static createReferencesVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startReferencesVector(builder: flatbuffers.Builder, numElems: number): void;
    static addGroups(builder: flatbuffers.Builder, groupsOffset: flatbuffers.Offset): void;
    static createGroupsVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startGroupsVector(builder: flatbuffers.Builder, numElems: number): void;
    static endVisualPromptPayload(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createVisualPromptPayload(builder: flatbuffers.Builder, referencesOffset: flatbuffers.Offset, groupsOffset: flatbuffers.Offset): flatbuffers.Offset;
    unpack(): VisualPromptPayloadT;
    unpackTo(_o: VisualPromptPayloadT): void;
}
export declare class VisualPromptPayloadT implements flatbuffers.IGeneratedObject {
    references: (PromptReferenceImageT)[];
    groups: (VisualPromptGroupT)[];
    constructor(references?: (PromptReferenceImageT)[], groups?: (VisualPromptGroupT)[]);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
