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
import { GazeAngles, GazeAnglesT } from '../../open-perception-kit/metadata/gaze-angles.js';
import { ObjectMeta, ObjectMetaT } from '../../open-perception-kit/metadata/object-meta.js';
export declare class GazeMeasurement implements flatbuffers.IUnpackableObject<GazeMeasurementT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): GazeMeasurement;
    static getRootAsGazeMeasurement(bb: flatbuffers.ByteBuffer, obj?: GazeMeasurement): GazeMeasurement;
    static getSizePrefixedRootAsGazeMeasurement(bb: flatbuffers.ByteBuffer, obj?: GazeMeasurement): GazeMeasurement;
    object(obj?: ObjectMeta): ObjectMeta | null;
    subjectId(): bigint | null;
    angles(obj?: GazeAngles): GazeAngles | null;
    confidence(): number | null;
    static startGazeMeasurement(builder: flatbuffers.Builder): void;
    static addObject(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset): void;
    static addSubjectId(builder: flatbuffers.Builder, subjectId: bigint): void;
    static addAngles(builder: flatbuffers.Builder, anglesOffset: flatbuffers.Offset): void;
    static addConfidence(builder: flatbuffers.Builder, confidence: number): void;
    static endGazeMeasurement(builder: flatbuffers.Builder): flatbuffers.Offset;
    unpack(): GazeMeasurementT;
    unpackTo(_o: GazeMeasurementT): void;
}
export declare class GazeMeasurementT implements flatbuffers.IGeneratedObject {
    object: ObjectMetaT | null;
    subjectId: bigint | null;
    angles: GazeAnglesT | null;
    confidence: number | null;
    constructor(object?: ObjectMetaT | null, subjectId?: bigint | null, angles?: GazeAnglesT | null, confidence?: number | null);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
