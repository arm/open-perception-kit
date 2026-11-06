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
export declare class ImageSpace implements flatbuffers.IUnpackableObject<ImageSpaceT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): ImageSpace;
    static getRootAsImageSpace(bb: flatbuffers.ByteBuffer, obj?: ImageSpace): ImageSpace;
    static getSizePrefixedRootAsImageSpace(bb: flatbuffers.ByteBuffer, obj?: ImageSpace): ImageSpace;
    frameId(): bigint;
    width(): number;
    height(): number;
    originX(): number;
    originY(): number;
    extentWidth(): number;
    extentHeight(): number;
    static startImageSpace(builder: flatbuffers.Builder): void;
    static addFrameId(builder: flatbuffers.Builder, frameId: bigint): void;
    static addWidth(builder: flatbuffers.Builder, width: number): void;
    static addHeight(builder: flatbuffers.Builder, height: number): void;
    static addOriginX(builder: flatbuffers.Builder, originX: number): void;
    static addOriginY(builder: flatbuffers.Builder, originY: number): void;
    static addExtentWidth(builder: flatbuffers.Builder, extentWidth: number): void;
    static addExtentHeight(builder: flatbuffers.Builder, extentHeight: number): void;
    static endImageSpace(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createImageSpace(builder: flatbuffers.Builder, frameId: bigint, width: number, height: number, originX: number, originY: number, extentWidth: number, extentHeight: number): flatbuffers.Offset;
    unpack(): ImageSpaceT;
    unpackTo(_o: ImageSpaceT): void;
}
export declare class ImageSpaceT implements flatbuffers.IGeneratedObject {
    frameId: bigint;
    width: number;
    height: number;
    originX: number;
    originY: number;
    extentWidth: number;
    extentHeight: number;
    constructor(frameId?: bigint, width?: number, height?: number, originX?: number, originY?: number, extentWidth?: number, extentHeight?: number);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
