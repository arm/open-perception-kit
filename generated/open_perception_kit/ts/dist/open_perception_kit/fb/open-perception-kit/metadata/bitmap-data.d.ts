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
export declare class BitmapData implements flatbuffers.IUnpackableObject<BitmapDataT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): BitmapData;
    static getRootAsBitmapData(bb: flatbuffers.ByteBuffer, obj?: BitmapData): BitmapData;
    static getSizePrefixedRootAsBitmapData(bb: flatbuffers.ByteBuffer, obj?: BitmapData): BitmapData;
    width(): number;
    height(): number;
    valueType(): string | null;
    valueType(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    pixels(index: number): number | null;
    pixelsLength(): number;
    pixelsArray(): Uint8Array | null;
    static startBitmapData(builder: flatbuffers.Builder): void;
    static addWidth(builder: flatbuffers.Builder, width: number): void;
    static addHeight(builder: flatbuffers.Builder, height: number): void;
    static addValueType(builder: flatbuffers.Builder, valueTypeOffset: flatbuffers.Offset): void;
    static addPixels(builder: flatbuffers.Builder, pixelsOffset: flatbuffers.Offset): void;
    static createPixelsVector(builder: flatbuffers.Builder, data: number[] | Uint8Array): flatbuffers.Offset;
    static startPixelsVector(builder: flatbuffers.Builder, numElems: number): void;
    static endBitmapData(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createBitmapData(builder: flatbuffers.Builder, width: number, height: number, valueTypeOffset: flatbuffers.Offset, pixelsOffset: flatbuffers.Offset): flatbuffers.Offset;
    unpack(): BitmapDataT;
    unpackTo(_o: BitmapDataT): void;
}
export declare class BitmapDataT implements flatbuffers.IGeneratedObject {
    width: number;
    height: number;
    valueType: string | Uint8Array | null;
    pixels: (number)[];
    constructor(width?: number, height?: number, valueType?: string | Uint8Array | null, pixels?: (number)[]);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
