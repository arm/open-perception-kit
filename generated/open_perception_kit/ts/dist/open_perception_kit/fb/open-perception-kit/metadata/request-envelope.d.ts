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
import { RequestApplication } from '../../open-perception-kit/metadata/request-application.js';
import { RequestDestination, RequestDestinationT } from '../../open-perception-kit/metadata/request-destination.js';
export declare class RequestEnvelope implements flatbuffers.IUnpackableObject<RequestEnvelopeT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): RequestEnvelope;
    static getRootAsRequestEnvelope(bb: flatbuffers.ByteBuffer, obj?: RequestEnvelope): RequestEnvelope;
    static getSizePrefixedRootAsRequestEnvelope(bb: flatbuffers.ByteBuffer, obj?: RequestEnvelope): RequestEnvelope;
    requestId(): bigint;
    revision(): bigint;
    destination(obj?: RequestDestination): RequestDestination | null;
    application(): RequestApplication;
    static startRequestEnvelope(builder: flatbuffers.Builder): void;
    static addRequestId(builder: flatbuffers.Builder, requestId: bigint): void;
    static addRevision(builder: flatbuffers.Builder, revision: bigint): void;
    static addDestination(builder: flatbuffers.Builder, destinationOffset: flatbuffers.Offset): void;
    static addApplication(builder: flatbuffers.Builder, application: RequestApplication): void;
    static endRequestEnvelope(builder: flatbuffers.Builder): flatbuffers.Offset;
    unpack(): RequestEnvelopeT;
    unpackTo(_o: RequestEnvelopeT): void;
}
export declare class RequestEnvelopeT implements flatbuffers.IGeneratedObject {
    requestId: bigint;
    revision: bigint;
    destination: RequestDestinationT | null;
    application: RequestApplication;
    constructor(requestId?: bigint, revision?: bigint, destination?: RequestDestinationT | null, application?: RequestApplication);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
