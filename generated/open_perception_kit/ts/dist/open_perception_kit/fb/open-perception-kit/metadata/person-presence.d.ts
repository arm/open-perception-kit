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
export declare class PersonPresence implements flatbuffers.IUnpackableObject<PersonPresenceT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): PersonPresence;
    static getRootAsPersonPresence(bb: flatbuffers.ByteBuffer, obj?: PersonPresence): PersonPresence;
    static getSizePrefixedRootAsPersonPresence(bb: flatbuffers.ByteBuffer, obj?: PersonPresence): PersonPresence;
    object(obj?: ObjectMeta): ObjectMeta | null;
    yesConfidence(): number;
    noConfidence(): number;
    static startPersonPresence(builder: flatbuffers.Builder): void;
    static addObject(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset): void;
    static addYesConfidence(builder: flatbuffers.Builder, yesConfidence: number): void;
    static addNoConfidence(builder: flatbuffers.Builder, noConfidence: number): void;
    static endPersonPresence(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createPersonPresence(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset, yesConfidence: number, noConfidence: number): flatbuffers.Offset;
    unpack(): PersonPresenceT;
    unpackTo(_o: PersonPresenceT): void;
}
export declare class PersonPresenceT implements flatbuffers.IGeneratedObject {
    object: ObjectMetaT | null;
    yesConfidence: number;
    noConfidence: number;
    constructor(object?: ObjectMetaT | null, yesConfidence?: number, noConfidence?: number);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
