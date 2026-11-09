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
import { TrackObservationState } from '../../open-perception-kit/metadata/track-observation-state.js';
import { TrackingTimeSource } from '../../open-perception-kit/metadata/tracking-time-source.js';
export declare class TrackingSample implements flatbuffers.IUnpackableObject<TrackingSampleT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): TrackingSample;
    static getRootAsTrackingSample(bb: flatbuffers.ByteBuffer, obj?: TrackingSample): TrackingSample;
    static getSizePrefixedRootAsTrackingSample(bb: flatbuffers.ByteBuffer, obj?: TrackingSample): TrackingSample;
    frameIndex(): bigint;
    elapsedMs(): number;
    timeSource(): TrackingTimeSource;
    state(): TrackObservationState;
    static startTrackingSample(builder: flatbuffers.Builder): void;
    static addFrameIndex(builder: flatbuffers.Builder, frameIndex: bigint): void;
    static addElapsedMs(builder: flatbuffers.Builder, elapsedMs: number): void;
    static addTimeSource(builder: flatbuffers.Builder, timeSource: TrackingTimeSource): void;
    static addState(builder: flatbuffers.Builder, state: TrackObservationState): void;
    static endTrackingSample(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createTrackingSample(builder: flatbuffers.Builder, frameIndex: bigint, elapsedMs: number, timeSource: TrackingTimeSource, state: TrackObservationState): flatbuffers.Offset;
    unpack(): TrackingSampleT;
    unpackTo(_o: TrackingSampleT): void;
}
export declare class TrackingSampleT implements flatbuffers.IGeneratedObject {
    frameIndex: bigint;
    elapsedMs: number;
    timeSource: TrackingTimeSource;
    state: TrackObservationState;
    constructor(frameIndex?: bigint, elapsedMs?: number, timeSource?: TrackingTimeSource, state?: TrackObservationState);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
