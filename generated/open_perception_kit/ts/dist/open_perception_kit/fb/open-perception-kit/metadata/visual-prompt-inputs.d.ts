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
import { RequestEnvelope, RequestEnvelopeT } from '../../open-perception-kit/metadata/request-envelope.js';
import { VisualPromptPayload, VisualPromptPayloadT } from '../../open-perception-kit/metadata/visual-prompt-payload.js';
export declare class VisualPromptInputs implements flatbuffers.IUnpackableObject<VisualPromptInputsT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): VisualPromptInputs;
    static getRootAsVisualPromptInputs(bb: flatbuffers.ByteBuffer, obj?: VisualPromptInputs): VisualPromptInputs;
    static getSizePrefixedRootAsVisualPromptInputs(bb: flatbuffers.ByteBuffer, obj?: VisualPromptInputs): VisualPromptInputs;
    static bufferHasIdentifier(bb: flatbuffers.ByteBuffer): boolean;
    schemaMajor(): number;
    schemaMinor(): number;
    request(obj?: RequestEnvelope): RequestEnvelope | null;
    payload(obj?: VisualPromptPayload): VisualPromptPayload | null;
    static startVisualPromptInputs(builder: flatbuffers.Builder): void;
    static addSchemaMajor(builder: flatbuffers.Builder, schemaMajor: number): void;
    static addSchemaMinor(builder: flatbuffers.Builder, schemaMinor: number): void;
    static addRequest(builder: flatbuffers.Builder, requestOffset: flatbuffers.Offset): void;
    static addPayload(builder: flatbuffers.Builder, payloadOffset: flatbuffers.Offset): void;
    static endVisualPromptInputs(builder: flatbuffers.Builder): flatbuffers.Offset;
    static finishVisualPromptInputsBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    static finishSizePrefixedVisualPromptInputsBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    unpack(): VisualPromptInputsT;
    unpackTo(_o: VisualPromptInputsT): void;
}
export declare class VisualPromptInputsT implements flatbuffers.IGeneratedObject {
    schemaMajor: number;
    schemaMinor: number;
    request: RequestEnvelopeT | null;
    payload: VisualPromptPayloadT | null;
    constructor(schemaMajor?: number, schemaMinor?: number, request?: RequestEnvelopeT | null, payload?: VisualPromptPayloadT | null);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
