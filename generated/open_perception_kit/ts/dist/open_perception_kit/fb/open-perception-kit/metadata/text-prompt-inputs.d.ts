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
import { TextPromptPayload, TextPromptPayloadT } from '../../open-perception-kit/metadata/text-prompt-payload.js';
export declare class TextPromptInputs implements flatbuffers.IUnpackableObject<TextPromptInputsT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): TextPromptInputs;
    static getRootAsTextPromptInputs(bb: flatbuffers.ByteBuffer, obj?: TextPromptInputs): TextPromptInputs;
    static getSizePrefixedRootAsTextPromptInputs(bb: flatbuffers.ByteBuffer, obj?: TextPromptInputs): TextPromptInputs;
    static bufferHasIdentifier(bb: flatbuffers.ByteBuffer): boolean;
    schemaMajor(): number;
    schemaMinor(): number;
    request(obj?: RequestEnvelope): RequestEnvelope | null;
    payload(obj?: TextPromptPayload): TextPromptPayload | null;
    static startTextPromptInputs(builder: flatbuffers.Builder): void;
    static addSchemaMajor(builder: flatbuffers.Builder, schemaMajor: number): void;
    static addSchemaMinor(builder: flatbuffers.Builder, schemaMinor: number): void;
    static addRequest(builder: flatbuffers.Builder, requestOffset: flatbuffers.Offset): void;
    static addPayload(builder: flatbuffers.Builder, payloadOffset: flatbuffers.Offset): void;
    static endTextPromptInputs(builder: flatbuffers.Builder): flatbuffers.Offset;
    static finishTextPromptInputsBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    static finishSizePrefixedTextPromptInputsBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    unpack(): TextPromptInputsT;
    unpackTo(_o: TextPromptInputsT): void;
}
export declare class TextPromptInputsT implements flatbuffers.IGeneratedObject {
    schemaMajor: number;
    schemaMinor: number;
    request: RequestEnvelopeT | null;
    payload: TextPromptPayloadT | null;
    constructor(schemaMajor?: number, schemaMinor?: number, request?: RequestEnvelopeT | null, payload?: TextPromptPayloadT | null);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
