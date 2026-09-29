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
import { ObjectEmbedding, ObjectEmbeddingT } from '../../open-perception-kit/metadata/object-embedding.js';
export declare class ObjectEmbeddings implements flatbuffers.IUnpackableObject<ObjectEmbeddingsT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): ObjectEmbeddings;
    static getRootAsObjectEmbeddings(bb: flatbuffers.ByteBuffer, obj?: ObjectEmbeddings): ObjectEmbeddings;
    static getSizePrefixedRootAsObjectEmbeddings(bb: flatbuffers.ByteBuffer, obj?: ObjectEmbeddings): ObjectEmbeddings;
    static bufferHasIdentifier(bb: flatbuffers.ByteBuffer): boolean;
    schemaMajor(): number;
    schemaMinor(): number;
    layer(obj?: LayerInfo): LayerInfo | null;
    embeddings(index: number, obj?: ObjectEmbedding): ObjectEmbedding | null;
    embeddingsLength(): number;
    static startObjectEmbeddings(builder: flatbuffers.Builder): void;
    static addSchemaMajor(builder: flatbuffers.Builder, schemaMajor: number): void;
    static addSchemaMinor(builder: flatbuffers.Builder, schemaMinor: number): void;
    static addLayer(builder: flatbuffers.Builder, layerOffset: flatbuffers.Offset): void;
    static addEmbeddings(builder: flatbuffers.Builder, embeddingsOffset: flatbuffers.Offset): void;
    static createEmbeddingsVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startEmbeddingsVector(builder: flatbuffers.Builder, numElems: number): void;
    static endObjectEmbeddings(builder: flatbuffers.Builder): flatbuffers.Offset;
    static finishObjectEmbeddingsBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    static finishSizePrefixedObjectEmbeddingsBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    unpack(): ObjectEmbeddingsT;
    unpackTo(_o: ObjectEmbeddingsT): void;
}
export declare class ObjectEmbeddingsT implements flatbuffers.IGeneratedObject {
    schemaMajor: number;
    schemaMinor: number;
    layer: LayerInfoT | null;
    embeddings: (ObjectEmbeddingT)[];
    constructor(schemaMajor?: number, schemaMinor?: number, layer?: LayerInfoT | null, embeddings?: (ObjectEmbeddingT)[]);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
