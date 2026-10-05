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
export declare class ProducerInfo implements flatbuffers.IUnpackableObject<ProducerInfoT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): ProducerInfo;
    static getRootAsProducerInfo(bb: flatbuffers.ByteBuffer, obj?: ProducerInfo): ProducerInfo;
    static getSizePrefixedRootAsProducerInfo(bb: flatbuffers.ByteBuffer, obj?: ProducerInfo): ProducerInfo;
    instanceId(): string | null;
    instanceId(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    component(): string | null;
    component(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    implementation(): string | null;
    implementation(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    static startProducerInfo(builder: flatbuffers.Builder): void;
    static addInstanceId(builder: flatbuffers.Builder, instanceIdOffset: flatbuffers.Offset): void;
    static addComponent(builder: flatbuffers.Builder, componentOffset: flatbuffers.Offset): void;
    static addImplementation(builder: flatbuffers.Builder, implementationOffset: flatbuffers.Offset): void;
    static endProducerInfo(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createProducerInfo(builder: flatbuffers.Builder, instanceIdOffset: flatbuffers.Offset, componentOffset: flatbuffers.Offset, implementationOffset: flatbuffers.Offset): flatbuffers.Offset;
    unpack(): ProducerInfoT;
    unpackTo(_o: ProducerInfoT): void;
}
export declare class ProducerInfoT implements flatbuffers.IGeneratedObject {
    instanceId: string | Uint8Array | null;
    component: string | Uint8Array | null;
    implementation: string | Uint8Array | null;
    constructor(instanceId?: string | Uint8Array | null, component?: string | Uint8Array | null, implementation?: string | Uint8Array | null);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
