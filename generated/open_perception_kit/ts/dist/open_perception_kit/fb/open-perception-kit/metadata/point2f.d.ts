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
export declare class Point2f implements flatbuffers.IUnpackableObject<Point2fT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): Point2f;
    static getRootAsPoint2f(bb: flatbuffers.ByteBuffer, obj?: Point2f): Point2f;
    static getSizePrefixedRootAsPoint2f(bb: flatbuffers.ByteBuffer, obj?: Point2f): Point2f;
    x(): number;
    y(): number;
    static startPoint2f(builder: flatbuffers.Builder): void;
    static addX(builder: flatbuffers.Builder, x: number): void;
    static addY(builder: flatbuffers.Builder, y: number): void;
    static endPoint2f(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createPoint2f(builder: flatbuffers.Builder, x: number, y: number): flatbuffers.Offset;
    unpack(): Point2fT;
    unpackTo(_o: Point2fT): void;
}
export declare class Point2fT implements flatbuffers.IGeneratedObject {
    x: number;
    y: number;
    constructor(x?: number, y?: number);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
