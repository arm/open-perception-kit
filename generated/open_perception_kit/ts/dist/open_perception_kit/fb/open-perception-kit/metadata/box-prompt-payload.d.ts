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
import { PromptBox, PromptBoxT } from '../../open-perception-kit/metadata/prompt-box.js';
export declare class BoxPromptPayload implements flatbuffers.IUnpackableObject<BoxPromptPayloadT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): BoxPromptPayload;
    static getRootAsBoxPromptPayload(bb: flatbuffers.ByteBuffer, obj?: BoxPromptPayload): BoxPromptPayload;
    static getSizePrefixedRootAsBoxPromptPayload(bb: flatbuffers.ByteBuffer, obj?: BoxPromptPayload): BoxPromptPayload;
    box(obj?: PromptBox): PromptBox | null;
    static startBoxPromptPayload(builder: flatbuffers.Builder): void;
    static addBox(builder: flatbuffers.Builder, boxOffset: flatbuffers.Offset): void;
    static endBoxPromptPayload(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createBoxPromptPayload(builder: flatbuffers.Builder, boxOffset: flatbuffers.Offset): flatbuffers.Offset;
    unpack(): BoxPromptPayloadT;
    unpackTo(_o: BoxPromptPayloadT): void;
}
export declare class BoxPromptPayloadT implements flatbuffers.IGeneratedObject {
    box: PromptBoxT | null;
    constructor(box?: PromptBoxT | null);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
