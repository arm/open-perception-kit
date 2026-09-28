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
import { WirePayload, WirePayloadT } from '../../open-perception-kit/internalfb/wire-payload.js';
export declare class WireEnvelope implements flatbuffers.IUnpackableObject<WireEnvelopeT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): WireEnvelope;
    static getRootAsWireEnvelope(bb: flatbuffers.ByteBuffer, obj?: WireEnvelope): WireEnvelope;
    static getSizePrefixedRootAsWireEnvelope(bb: flatbuffers.ByteBuffer, obj?: WireEnvelope): WireEnvelope;
    static bufferHasIdentifier(bb: flatbuffers.ByteBuffer): boolean;
    payloads(index: number, obj?: WirePayload): WirePayload | null;
    payloadsLength(): number;
    producerSdkName(): string | null;
    producerSdkName(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    producerSdkVersion(): string | null;
    producerSdkVersion(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    producerSchemaSetSha256(): string | null;
    producerSchemaSetSha256(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    static startWireEnvelope(builder: flatbuffers.Builder): void;
    static addPayloads(builder: flatbuffers.Builder, payloadsOffset: flatbuffers.Offset): void;
    static createPayloadsVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startPayloadsVector(builder: flatbuffers.Builder, numElems: number): void;
    static addProducerSdkName(builder: flatbuffers.Builder, producerSdkNameOffset: flatbuffers.Offset): void;
    static addProducerSdkVersion(builder: flatbuffers.Builder, producerSdkVersionOffset: flatbuffers.Offset): void;
    static addProducerSchemaSetSha256(builder: flatbuffers.Builder, producerSchemaSetSha256Offset: flatbuffers.Offset): void;
    static endWireEnvelope(builder: flatbuffers.Builder): flatbuffers.Offset;
    static finishWireEnvelopeBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    static finishSizePrefixedWireEnvelopeBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    static createWireEnvelope(builder: flatbuffers.Builder, payloadsOffset: flatbuffers.Offset, producerSdkNameOffset: flatbuffers.Offset, producerSdkVersionOffset: flatbuffers.Offset, producerSchemaSetSha256Offset: flatbuffers.Offset): flatbuffers.Offset;
    unpack(): WireEnvelopeT;
    unpackTo(_o: WireEnvelopeT): void;
}
export declare class WireEnvelopeT implements flatbuffers.IGeneratedObject {
    payloads: (WirePayloadT)[];
    producerSdkName: string | Uint8Array | null;
    producerSdkVersion: string | Uint8Array | null;
    producerSchemaSetSha256: string | Uint8Array | null;
    constructor(payloads?: (WirePayloadT)[], producerSdkName?: string | Uint8Array | null, producerSdkVersion?: string | Uint8Array | null, producerSchemaSetSha256?: string | Uint8Array | null);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
