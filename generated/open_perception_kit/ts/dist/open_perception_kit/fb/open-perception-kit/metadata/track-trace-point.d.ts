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
import { TrackingSample, TrackingSampleT } from '../../open-perception-kit/metadata/tracking-sample.js';
export declare class TrackTracePoint implements flatbuffers.IUnpackableObject<TrackTracePointT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): TrackTracePoint;
    static getRootAsTrackTracePoint(bb: flatbuffers.ByteBuffer, obj?: TrackTracePoint): TrackTracePoint;
    static getSizePrefixedRootAsTrackTracePoint(bb: flatbuffers.ByteBuffer, obj?: TrackTracePoint): TrackTracePoint;
    x(): number;
    y(): number;
    sample(obj?: TrackingSample): TrackingSample | null;
    static startTrackTracePoint(builder: flatbuffers.Builder): void;
    static addX(builder: flatbuffers.Builder, x: number): void;
    static addY(builder: flatbuffers.Builder, y: number): void;
    static addSample(builder: flatbuffers.Builder, sampleOffset: flatbuffers.Offset): void;
    static endTrackTracePoint(builder: flatbuffers.Builder): flatbuffers.Offset;
    unpack(): TrackTracePointT;
    unpackTo(_o: TrackTracePointT): void;
}
export declare class TrackTracePointT implements flatbuffers.IGeneratedObject {
    x: number;
    y: number;
    sample: TrackingSampleT | null;
    constructor(x?: number, y?: number, sample?: TrackingSampleT | null);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
