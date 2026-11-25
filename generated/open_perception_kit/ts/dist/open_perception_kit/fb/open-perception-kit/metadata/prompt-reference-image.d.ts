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
import { PromptReferenceEncoding } from '../../open-perception-kit/metadata/prompt-reference-encoding.js';
export declare class PromptReferenceImage implements flatbuffers.IUnpackableObject<PromptReferenceImageT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): PromptReferenceImage;
    static getRootAsPromptReferenceImage(bb: flatbuffers.ByteBuffer, obj?: PromptReferenceImage): PromptReferenceImage;
    static getSizePrefixedRootAsPromptReferenceImage(bb: flatbuffers.ByteBuffer, obj?: PromptReferenceImage): PromptReferenceImage;
    width(): number;
    height(): number;
    encoding(): PromptReferenceEncoding;
    pixels(index: number): number | null;
    pixelsLength(): number;
    pixelsArray(): Uint8Array | null;
    static startPromptReferenceImage(builder: flatbuffers.Builder): void;
    static addWidth(builder: flatbuffers.Builder, width: number): void;
    static addHeight(builder: flatbuffers.Builder, height: number): void;
    static addEncoding(builder: flatbuffers.Builder, encoding: PromptReferenceEncoding): void;
    static addPixels(builder: flatbuffers.Builder, pixelsOffset: flatbuffers.Offset): void;
    static createPixelsVector(builder: flatbuffers.Builder, data: number[] | Uint8Array): flatbuffers.Offset;
    static startPixelsVector(builder: flatbuffers.Builder, numElems: number): void;
    static endPromptReferenceImage(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createPromptReferenceImage(builder: flatbuffers.Builder, width: number, height: number, encoding: PromptReferenceEncoding, pixelsOffset: flatbuffers.Offset): flatbuffers.Offset;
    unpack(): PromptReferenceImageT;
    unpackTo(_o: PromptReferenceImageT): void;
}
export declare class PromptReferenceImageT implements flatbuffers.IGeneratedObject {
    width: number;
    height: number;
    encoding: PromptReferenceEncoding;
    pixels: (number)[];
    constructor(width?: number, height?: number, encoding?: PromptReferenceEncoding, pixels?: (number)[]);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
