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
export declare class PoseEstimation implements flatbuffers.IUnpackableObject<PoseEstimationT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): PoseEstimation;
    static getRootAsPoseEstimation(bb: flatbuffers.ByteBuffer, obj?: PoseEstimation): PoseEstimation;
    static getSizePrefixedRootAsPoseEstimation(bb: flatbuffers.ByteBuffer, obj?: PoseEstimation): PoseEstimation;
    object(obj?: ObjectMeta): ObjectMeta | null;
    confidence(): number;
    yaw(): number;
    pitch(): number;
    static startPoseEstimation(builder: flatbuffers.Builder): void;
    static addObject(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset): void;
    static addConfidence(builder: flatbuffers.Builder, confidence: number): void;
    static addYaw(builder: flatbuffers.Builder, yaw: number): void;
    static addPitch(builder: flatbuffers.Builder, pitch: number): void;
    static endPoseEstimation(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createPoseEstimation(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset, confidence: number, yaw: number, pitch: number): flatbuffers.Offset;
    unpack(): PoseEstimationT;
    unpackTo(_o: PoseEstimationT): void;
}
export declare class PoseEstimationT implements flatbuffers.IGeneratedObject {
    object: ObjectMetaT | null;
    confidence: number;
    yaw: number;
    pitch: number;
    constructor(object?: ObjectMetaT | null, confidence?: number, yaw?: number, pitch?: number);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
