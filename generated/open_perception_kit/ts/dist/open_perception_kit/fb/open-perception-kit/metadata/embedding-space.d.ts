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
import { EmbeddingMetric } from '../../open-perception-kit/metadata/embedding-metric.js';
export declare class EmbeddingSpace implements flatbuffers.IUnpackableObject<EmbeddingSpaceT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): EmbeddingSpace;
    static getRootAsEmbeddingSpace(bb: flatbuffers.ByteBuffer, obj?: EmbeddingSpace): EmbeddingSpace;
    static getSizePrefixedRootAsEmbeddingSpace(bb: flatbuffers.ByteBuffer, obj?: EmbeddingSpace): EmbeddingSpace;
    id(): string | null;
    id(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    dimensions(): number;
    metric(): EmbeddingMetric;
    static startEmbeddingSpace(builder: flatbuffers.Builder): void;
    static addId(builder: flatbuffers.Builder, idOffset: flatbuffers.Offset): void;
    static addDimensions(builder: flatbuffers.Builder, dimensions: number): void;
    static addMetric(builder: flatbuffers.Builder, metric: EmbeddingMetric): void;
    static endEmbeddingSpace(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createEmbeddingSpace(builder: flatbuffers.Builder, idOffset: flatbuffers.Offset, dimensions: number, metric: EmbeddingMetric): flatbuffers.Offset;
    unpack(): EmbeddingSpaceT;
    unpackTo(_o: EmbeddingSpaceT): void;
}
export declare class EmbeddingSpaceT implements flatbuffers.IGeneratedObject {
    id: string | Uint8Array | null;
    dimensions: number;
    metric: EmbeddingMetric;
    constructor(id?: string | Uint8Array | null, dimensions?: number, metric?: EmbeddingMetric);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
