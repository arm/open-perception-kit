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
export declare class ObjectMeta implements flatbuffers.IUnpackableObject<ObjectMetaT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): ObjectMeta;
    static getRootAsObjectMeta(bb: flatbuffers.ByteBuffer, obj?: ObjectMeta): ObjectMeta;
    static getSizePrefixedRootAsObjectMeta(bb: flatbuffers.ByteBuffer, obj?: ObjectMeta): ObjectMeta;
    id(): bigint;
    parentId(): bigint;
    creationTsNs(): bigint;
    static startObjectMeta(builder: flatbuffers.Builder): void;
    static addId(builder: flatbuffers.Builder, id: bigint): void;
    static addParentId(builder: flatbuffers.Builder, parentId: bigint): void;
    static addCreationTsNs(builder: flatbuffers.Builder, creationTsNs: bigint): void;
    static endObjectMeta(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createObjectMeta(builder: flatbuffers.Builder, id: bigint, parentId: bigint, creationTsNs: bigint): flatbuffers.Offset;
    unpack(): ObjectMetaT;
    unpackTo(_o: ObjectMetaT): void;
}
export declare class ObjectMetaT implements flatbuffers.IGeneratedObject {
    id: bigint;
    parentId: bigint;
    creationTsNs: bigint;
    constructor(id?: bigint, parentId?: bigint, creationTsNs?: bigint);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
