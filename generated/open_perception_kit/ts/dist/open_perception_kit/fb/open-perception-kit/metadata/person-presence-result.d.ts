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
export declare class PersonPresenceResult implements flatbuffers.IUnpackableObject<PersonPresenceResultT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): PersonPresenceResult;
    static getRootAsPersonPresenceResult(bb: flatbuffers.ByteBuffer, obj?: PersonPresenceResult): PersonPresenceResult;
    static getSizePrefixedRootAsPersonPresenceResult(bb: flatbuffers.ByteBuffer, obj?: PersonPresenceResult): PersonPresenceResult;
    personPresent(): boolean;
    yesScore(): number | null;
    noScore(): number | null;
    static startPersonPresenceResult(builder: flatbuffers.Builder): void;
    static addPersonPresent(builder: flatbuffers.Builder, personPresent: boolean): void;
    static addYesScore(builder: flatbuffers.Builder, yesScore: number): void;
    static addNoScore(builder: flatbuffers.Builder, noScore: number): void;
    static endPersonPresenceResult(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createPersonPresenceResult(builder: flatbuffers.Builder, personPresent: boolean, yesScore: number | null, noScore: number | null): flatbuffers.Offset;
    unpack(): PersonPresenceResultT;
    unpackTo(_o: PersonPresenceResultT): void;
}
export declare class PersonPresenceResultT implements flatbuffers.IGeneratedObject {
    personPresent: boolean;
    yesScore: number | null;
    noScore: number | null;
    constructor(personPresent?: boolean, yesScore?: number | null, noScore?: number | null);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
