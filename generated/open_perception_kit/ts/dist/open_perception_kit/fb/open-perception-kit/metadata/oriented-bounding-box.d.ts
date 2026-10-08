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
export declare class OrientedBoundingBox implements flatbuffers.IUnpackableObject<OrientedBoundingBoxT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): OrientedBoundingBox;
    static getRootAsOrientedBoundingBox(bb: flatbuffers.ByteBuffer, obj?: OrientedBoundingBox): OrientedBoundingBox;
    static getSizePrefixedRootAsOrientedBoundingBox(bb: flatbuffers.ByteBuffer, obj?: OrientedBoundingBox): OrientedBoundingBox;
    centerX(): number;
    centerY(): number;
    width(): number;
    height(): number;
    angleRadians(): number;
    static startOrientedBoundingBox(builder: flatbuffers.Builder): void;
    static addCenterX(builder: flatbuffers.Builder, centerX: number): void;
    static addCenterY(builder: flatbuffers.Builder, centerY: number): void;
    static addWidth(builder: flatbuffers.Builder, width: number): void;
    static addHeight(builder: flatbuffers.Builder, height: number): void;
    static addAngleRadians(builder: flatbuffers.Builder, angleRadians: number): void;
    static endOrientedBoundingBox(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createOrientedBoundingBox(builder: flatbuffers.Builder, centerX: number, centerY: number, width: number, height: number, angleRadians: number): flatbuffers.Offset;
    unpack(): OrientedBoundingBoxT;
    unpackTo(_o: OrientedBoundingBoxT): void;
}
export declare class OrientedBoundingBoxT implements flatbuffers.IGeneratedObject {
    centerX: number;
    centerY: number;
    width: number;
    height: number;
    angleRadians: number;
    constructor(centerX?: number, centerY?: number, width?: number, height?: number, angleRadians?: number);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
