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
export declare class CameraContactResult implements flatbuffers.IUnpackableObject<CameraContactResultT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): CameraContactResult;
    static getRootAsCameraContactResult(bb: flatbuffers.ByteBuffer, obj?: CameraContactResult): CameraContactResult;
    static getSizePrefixedRootAsCameraContactResult(bb: flatbuffers.ByteBuffer, obj?: CameraContactResult): CameraContactResult;
    inContact(): boolean;
    confidence(): number | null;
    modelClassIndex(): number | null;
    static startCameraContactResult(builder: flatbuffers.Builder): void;
    static addInContact(builder: flatbuffers.Builder, inContact: boolean): void;
    static addConfidence(builder: flatbuffers.Builder, confidence: number): void;
    static addModelClassIndex(builder: flatbuffers.Builder, modelClassIndex: number): void;
    static endCameraContactResult(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createCameraContactResult(builder: flatbuffers.Builder, inContact: boolean, confidence: number | null, modelClassIndex: number | null): flatbuffers.Offset;
    unpack(): CameraContactResultT;
    unpackTo(_o: CameraContactResultT): void;
}
export declare class CameraContactResultT implements flatbuffers.IGeneratedObject {
    inContact: boolean;
    confidence: number | null;
    modelClassIndex: number | null;
    constructor(inContact?: boolean, confidence?: number | null, modelClassIndex?: number | null);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
