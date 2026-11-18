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
import { CameraContactResult, CameraContactResultT } from '../../open-perception-kit/metadata/camera-contact-result.js';
import { ObjectMeta, ObjectMetaT } from '../../open-perception-kit/metadata/object-meta.js';
export declare class CameraContact implements flatbuffers.IUnpackableObject<CameraContactT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): CameraContact;
    static getRootAsCameraContact(bb: flatbuffers.ByteBuffer, obj?: CameraContact): CameraContact;
    static getSizePrefixedRootAsCameraContact(bb: flatbuffers.ByteBuffer, obj?: CameraContact): CameraContact;
    object(obj?: ObjectMeta): ObjectMeta | null;
    result(obj?: CameraContactResult): CameraContactResult | null;
    subjectId(): bigint | null;
    static startCameraContact(builder: flatbuffers.Builder): void;
    static addObject(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset): void;
    static addResult(builder: flatbuffers.Builder, resultOffset: flatbuffers.Offset): void;
    static addSubjectId(builder: flatbuffers.Builder, subjectId: bigint): void;
    static endCameraContact(builder: flatbuffers.Builder): flatbuffers.Offset;
    unpack(): CameraContactT;
    unpackTo(_o: CameraContactT): void;
}
export declare class CameraContactT implements flatbuffers.IGeneratedObject {
    object: ObjectMetaT | null;
    result: CameraContactResultT | null;
    subjectId: bigint | null;
    constructor(object?: ObjectMetaT | null, result?: CameraContactResultT | null, subjectId?: bigint | null);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
