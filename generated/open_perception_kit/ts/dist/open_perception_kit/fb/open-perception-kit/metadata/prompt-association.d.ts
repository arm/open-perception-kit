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
import { LayerInfo, LayerInfoT } from '../../open-perception-kit/metadata/layer-info.js';
import { RequestEnvelope, RequestEnvelopeT } from '../../open-perception-kit/metadata/request-envelope.js';
export declare class PromptAssociation implements flatbuffers.IUnpackableObject<PromptAssociationT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): PromptAssociation;
    static getRootAsPromptAssociation(bb: flatbuffers.ByteBuffer, obj?: PromptAssociation): PromptAssociation;
    static getSizePrefixedRootAsPromptAssociation(bb: flatbuffers.ByteBuffer, obj?: PromptAssociation): PromptAssociation;
    static bufferHasIdentifier(bb: flatbuffers.ByteBuffer): boolean;
    schemaMajor(): number;
    schemaMinor(): number;
    layer(obj?: LayerInfo): LayerInfo | null;
    request(obj?: RequestEnvelope): RequestEnvelope | null;
    sessionId(): string | null;
    sessionId(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    parentId(): bigint;
    static startPromptAssociation(builder: flatbuffers.Builder): void;
    static addSchemaMajor(builder: flatbuffers.Builder, schemaMajor: number): void;
    static addSchemaMinor(builder: flatbuffers.Builder, schemaMinor: number): void;
    static addLayer(builder: flatbuffers.Builder, layerOffset: flatbuffers.Offset): void;
    static addRequest(builder: flatbuffers.Builder, requestOffset: flatbuffers.Offset): void;
    static addSessionId(builder: flatbuffers.Builder, sessionIdOffset: flatbuffers.Offset): void;
    static addParentId(builder: flatbuffers.Builder, parentId: bigint): void;
    static endPromptAssociation(builder: flatbuffers.Builder): flatbuffers.Offset;
    static finishPromptAssociationBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    static finishSizePrefixedPromptAssociationBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    unpack(): PromptAssociationT;
    unpackTo(_o: PromptAssociationT): void;
}
export declare class PromptAssociationT implements flatbuffers.IGeneratedObject {
    schemaMajor: number;
    schemaMinor: number;
    layer: LayerInfoT | null;
    request: RequestEnvelopeT | null;
    sessionId: string | Uint8Array | null;
    parentId: bigint;
    constructor(schemaMajor?: number, schemaMinor?: number, layer?: LayerInfoT | null, request?: RequestEnvelopeT | null, sessionId?: string | Uint8Array | null, parentId?: bigint);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
