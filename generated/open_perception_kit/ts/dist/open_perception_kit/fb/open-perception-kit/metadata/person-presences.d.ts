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
import { PersonPresence, PersonPresenceT } from '../../open-perception-kit/metadata/person-presence.js';
export declare class PersonPresences implements flatbuffers.IUnpackableObject<PersonPresencesT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): PersonPresences;
    static getRootAsPersonPresences(bb: flatbuffers.ByteBuffer, obj?: PersonPresences): PersonPresences;
    static getSizePrefixedRootAsPersonPresences(bb: flatbuffers.ByteBuffer, obj?: PersonPresences): PersonPresences;
    static bufferHasIdentifier(bb: flatbuffers.ByteBuffer): boolean;
    schemaMajor(): number;
    schemaMinor(): number;
    layer(obj?: LayerInfo): LayerInfo | null;
    presences(index: number, obj?: PersonPresence): PersonPresence | null;
    presencesLength(): number;
    static startPersonPresences(builder: flatbuffers.Builder): void;
    static addSchemaMajor(builder: flatbuffers.Builder, schemaMajor: number): void;
    static addSchemaMinor(builder: flatbuffers.Builder, schemaMinor: number): void;
    static addLayer(builder: flatbuffers.Builder, layerOffset: flatbuffers.Offset): void;
    static addPresences(builder: flatbuffers.Builder, presencesOffset: flatbuffers.Offset): void;
    static createPresencesVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startPresencesVector(builder: flatbuffers.Builder, numElems: number): void;
    static endPersonPresences(builder: flatbuffers.Builder): flatbuffers.Offset;
    static finishPersonPresencesBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    static finishSizePrefixedPersonPresencesBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    unpack(): PersonPresencesT;
    unpackTo(_o: PersonPresencesT): void;
}
export declare class PersonPresencesT implements flatbuffers.IGeneratedObject {
    schemaMajor: number;
    schemaMinor: number;
    layer: LayerInfoT | null;
    presences: (PersonPresenceT)[];
    constructor(schemaMajor?: number, schemaMinor?: number, layer?: LayerInfoT | null, presences?: (PersonPresenceT)[]);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
