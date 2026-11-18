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
export declare class RequestDestination implements flatbuffers.IUnpackableObject<RequestDestinationT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): RequestDestination;
    static getRootAsRequestDestination(bb: flatbuffers.ByteBuffer, obj?: RequestDestination): RequestDestination;
    static getSizePrefixedRootAsRequestDestination(bb: flatbuffers.ByteBuffer, obj?: RequestDestination): RequestDestination;
    inferenceInstance(): string | null;
    inferenceInstance(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    sourceName(): string | null;
    sourceName(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    static startRequestDestination(builder: flatbuffers.Builder): void;
    static addInferenceInstance(builder: flatbuffers.Builder, inferenceInstanceOffset: flatbuffers.Offset): void;
    static addSourceName(builder: flatbuffers.Builder, sourceNameOffset: flatbuffers.Offset): void;
    static endRequestDestination(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createRequestDestination(builder: flatbuffers.Builder, inferenceInstanceOffset: flatbuffers.Offset, sourceNameOffset: flatbuffers.Offset): flatbuffers.Offset;
    unpack(): RequestDestinationT;
    unpackTo(_o: RequestDestinationT): void;
}
export declare class RequestDestinationT implements flatbuffers.IGeneratedObject {
    inferenceInstance: string | Uint8Array | null;
    sourceName: string | Uint8Array | null;
    constructor(inferenceInstance?: string | Uint8Array | null, sourceName?: string | Uint8Array | null);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
