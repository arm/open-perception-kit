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
import { ObjectMeta, ObjectMetaT } from '../../open-perception-kit/metadata/object-meta.js';
export declare class VideoFrameContext implements flatbuffers.IUnpackableObject<VideoFrameContextT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): VideoFrameContext;
    static getRootAsVideoFrameContext(bb: flatbuffers.ByteBuffer, obj?: VideoFrameContext): VideoFrameContext;
    static getSizePrefixedRootAsVideoFrameContext(bb: flatbuffers.ByteBuffer, obj?: VideoFrameContext): VideoFrameContext;
    object(obj?: ObjectMeta): ObjectMeta | null;
    originalWidth(): bigint;
    originalHeight(): bigint;
    sourceCropLeft(): bigint;
    sourceCropRight(): bigint;
    sourceCropTop(): bigint;
    sourceCropBottom(): bigint;
    letterboxLeft(): bigint;
    letterboxRight(): bigint;
    letterboxTop(): bigint;
    letterboxBottom(): bigint;
    static startVideoFrameContext(builder: flatbuffers.Builder): void;
    static addObject(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset): void;
    static addOriginalWidth(builder: flatbuffers.Builder, originalWidth: bigint): void;
    static addOriginalHeight(builder: flatbuffers.Builder, originalHeight: bigint): void;
    static addSourceCropLeft(builder: flatbuffers.Builder, sourceCropLeft: bigint): void;
    static addSourceCropRight(builder: flatbuffers.Builder, sourceCropRight: bigint): void;
    static addSourceCropTop(builder: flatbuffers.Builder, sourceCropTop: bigint): void;
    static addSourceCropBottom(builder: flatbuffers.Builder, sourceCropBottom: bigint): void;
    static addLetterboxLeft(builder: flatbuffers.Builder, letterboxLeft: bigint): void;
    static addLetterboxRight(builder: flatbuffers.Builder, letterboxRight: bigint): void;
    static addLetterboxTop(builder: flatbuffers.Builder, letterboxTop: bigint): void;
    static addLetterboxBottom(builder: flatbuffers.Builder, letterboxBottom: bigint): void;
    static endVideoFrameContext(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createVideoFrameContext(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset, originalWidth: bigint, originalHeight: bigint, sourceCropLeft: bigint, sourceCropRight: bigint, sourceCropTop: bigint, sourceCropBottom: bigint, letterboxLeft: bigint, letterboxRight: bigint, letterboxTop: bigint, letterboxBottom: bigint): flatbuffers.Offset;
    unpack(): VideoFrameContextT;
    unpackTo(_o: VideoFrameContextT): void;
}
export declare class VideoFrameContextT implements flatbuffers.IGeneratedObject {
    object: ObjectMetaT | null;
    originalWidth: bigint;
    originalHeight: bigint;
    sourceCropLeft: bigint;
    sourceCropRight: bigint;
    sourceCropTop: bigint;
    sourceCropBottom: bigint;
    letterboxLeft: bigint;
    letterboxRight: bigint;
    letterboxTop: bigint;
    letterboxBottom: bigint;
    constructor(object?: ObjectMetaT | null, originalWidth?: bigint, originalHeight?: bigint, sourceCropLeft?: bigint, sourceCropRight?: bigint, sourceCropTop?: bigint, sourceCropBottom?: bigint, letterboxLeft?: bigint, letterboxRight?: bigint, letterboxTop?: bigint, letterboxBottom?: bigint);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
