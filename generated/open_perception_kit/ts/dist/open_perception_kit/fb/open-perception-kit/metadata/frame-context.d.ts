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
import { AudioFrameContext, AudioFrameContextT } from '../../open-perception-kit/metadata/audio-frame-context.js';
import { LayerInfo, LayerInfoT } from '../../open-perception-kit/metadata/layer-info.js';
import { VideoFrameContext, VideoFrameContextT } from '../../open-perception-kit/metadata/video-frame-context.js';
export declare class FrameContext implements flatbuffers.IUnpackableObject<FrameContextT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): FrameContext;
    static getRootAsFrameContext(bb: flatbuffers.ByteBuffer, obj?: FrameContext): FrameContext;
    static getSizePrefixedRootAsFrameContext(bb: flatbuffers.ByteBuffer, obj?: FrameContext): FrameContext;
    static bufferHasIdentifier(bb: flatbuffers.ByteBuffer): boolean;
    schemaMajor(): number;
    schemaMinor(): number;
    layer(obj?: LayerInfo): LayerInfo | null;
    video(obj?: VideoFrameContext): VideoFrameContext | null;
    audio(obj?: AudioFrameContext): AudioFrameContext | null;
    static startFrameContext(builder: flatbuffers.Builder): void;
    static addSchemaMajor(builder: flatbuffers.Builder, schemaMajor: number): void;
    static addSchemaMinor(builder: flatbuffers.Builder, schemaMinor: number): void;
    static addLayer(builder: flatbuffers.Builder, layerOffset: flatbuffers.Offset): void;
    static addVideo(builder: flatbuffers.Builder, videoOffset: flatbuffers.Offset): void;
    static addAudio(builder: flatbuffers.Builder, audioOffset: flatbuffers.Offset): void;
    static endFrameContext(builder: flatbuffers.Builder): flatbuffers.Offset;
    static finishFrameContextBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    static finishSizePrefixedFrameContextBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    unpack(): FrameContextT;
    unpackTo(_o: FrameContextT): void;
}
export declare class FrameContextT implements flatbuffers.IGeneratedObject {
    schemaMajor: number;
    schemaMinor: number;
    layer: LayerInfoT | null;
    video: VideoFrameContextT | null;
    audio: AudioFrameContextT | null;
    constructor(schemaMajor?: number, schemaMinor?: number, layer?: LayerInfoT | null, video?: VideoFrameContextT | null, audio?: AudioFrameContextT | null);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
