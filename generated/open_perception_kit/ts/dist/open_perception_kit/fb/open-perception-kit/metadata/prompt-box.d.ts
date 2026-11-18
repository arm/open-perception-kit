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
export declare class PromptBox implements flatbuffers.IUnpackableObject<PromptBoxT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): PromptBox;
    static getRootAsPromptBox(bb: flatbuffers.ByteBuffer, obj?: PromptBox): PromptBox;
    static getSizePrefixedRootAsPromptBox(bb: flatbuffers.ByteBuffer, obj?: PromptBox): PromptBox;
    left(): number;
    top(): number;
    right(): number;
    bottom(): number;
    static startPromptBox(builder: flatbuffers.Builder): void;
    static addLeft(builder: flatbuffers.Builder, left: number): void;
    static addTop(builder: flatbuffers.Builder, top: number): void;
    static addRight(builder: flatbuffers.Builder, right: number): void;
    static addBottom(builder: flatbuffers.Builder, bottom: number): void;
    static endPromptBox(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createPromptBox(builder: flatbuffers.Builder, left: number, top: number, right: number, bottom: number): flatbuffers.Offset;
    unpack(): PromptBoxT;
    unpackTo(_o: PromptBoxT): void;
}
export declare class PromptBoxT implements flatbuffers.IGeneratedObject {
    left: number;
    top: number;
    right: number;
    bottom: number;
    constructor(left?: number, top?: number, right?: number, bottom?: number);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
