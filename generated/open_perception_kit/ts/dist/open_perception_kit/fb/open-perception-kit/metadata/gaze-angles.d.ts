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
export declare class GazeAngles implements flatbuffers.IUnpackableObject<GazeAnglesT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): GazeAngles;
    static getRootAsGazeAngles(bb: flatbuffers.ByteBuffer, obj?: GazeAngles): GazeAngles;
    static getSizePrefixedRootAsGazeAngles(bb: flatbuffers.ByteBuffer, obj?: GazeAngles): GazeAngles;
    yawDegrees(): number;
    pitchDegrees(): number;
    static startGazeAngles(builder: flatbuffers.Builder): void;
    static addYawDegrees(builder: flatbuffers.Builder, yawDegrees: number): void;
    static addPitchDegrees(builder: flatbuffers.Builder, pitchDegrees: number): void;
    static endGazeAngles(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createGazeAngles(builder: flatbuffers.Builder, yawDegrees: number, pitchDegrees: number): flatbuffers.Offset;
    unpack(): GazeAnglesT;
    unpackTo(_o: GazeAnglesT): void;
}
export declare class GazeAnglesT implements flatbuffers.IGeneratedObject {
    yawDegrees: number;
    pitchDegrees: number;
    constructor(yawDegrees?: number, pitchDegrees?: number);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
