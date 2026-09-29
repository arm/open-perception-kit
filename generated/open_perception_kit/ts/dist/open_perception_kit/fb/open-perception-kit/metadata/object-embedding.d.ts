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
import { ObjectMeta, ObjectMetaT } from '../../open-perception-kit/metadata/object-meta.js';
export declare class ObjectEmbedding implements flatbuffers.IUnpackableObject<ObjectEmbeddingT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): ObjectEmbedding;
    static getRootAsObjectEmbedding(bb: flatbuffers.ByteBuffer, obj?: ObjectEmbedding): ObjectEmbedding;
    static getSizePrefixedRootAsObjectEmbedding(bb: flatbuffers.ByteBuffer, obj?: ObjectEmbedding): ObjectEmbedding;
    object(obj?: ObjectMeta): ObjectMeta | null;
    values(index: number): number | null;
    valuesLength(): number;
    valuesArray(): Float32Array | null;
    static startObjectEmbedding(builder: flatbuffers.Builder): void;
    static addObject(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset): void;
    static addValues(builder: flatbuffers.Builder, valuesOffset: flatbuffers.Offset): void;
    static createValuesVector(builder: flatbuffers.Builder, data: number[] | Float32Array): flatbuffers.Offset;
    /**
     * @deprecated This Uint8Array overload will be removed in the future.
     */
    static createValuesVector(builder: flatbuffers.Builder, data: number[] | Uint8Array): flatbuffers.Offset;
    static startValuesVector(builder: flatbuffers.Builder, numElems: number): void;
    static endObjectEmbedding(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createObjectEmbedding(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset, valuesOffset: flatbuffers.Offset): flatbuffers.Offset;
    unpack(): ObjectEmbeddingT;
    unpackTo(_o: ObjectEmbeddingT): void;
}
export declare class ObjectEmbeddingT implements flatbuffers.IGeneratedObject {
    object: ObjectMetaT | null;
    values: (number)[];
    constructor(object?: ObjectMetaT | null, values?: (number)[]);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
