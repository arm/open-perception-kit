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
export declare class FrameRate implements flatbuffers.IUnpackableObject<FrameRateT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): FrameRate;
    static getRootAsFrameRate(bb: flatbuffers.ByteBuffer, obj?: FrameRate): FrameRate;
    static getSizePrefixedRootAsFrameRate(bb: flatbuffers.ByteBuffer, obj?: FrameRate): FrameRate;
    fps(): number | null;
    static startFrameRate(builder: flatbuffers.Builder): void;
    static addFps(builder: flatbuffers.Builder, fps: number): void;
    static endFrameRate(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createFrameRate(builder: flatbuffers.Builder, fps: number | null): flatbuffers.Offset;
    unpack(): FrameRateT;
    unpackTo(_o: FrameRateT): void;
}
export declare class FrameRateT implements flatbuffers.IGeneratedObject {
    fps: number | null;
    constructor(fps?: number | null);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
