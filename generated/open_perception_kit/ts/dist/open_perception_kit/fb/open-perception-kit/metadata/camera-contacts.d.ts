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
import { CameraContact, CameraContactT } from '../../open-perception-kit/metadata/camera-contact.js';
import { LayerInfo, LayerInfoT } from '../../open-perception-kit/metadata/layer-info.js';
export declare class CameraContacts implements flatbuffers.IUnpackableObject<CameraContactsT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): CameraContacts;
    static getRootAsCameraContacts(bb: flatbuffers.ByteBuffer, obj?: CameraContacts): CameraContacts;
    static getSizePrefixedRootAsCameraContacts(bb: flatbuffers.ByteBuffer, obj?: CameraContacts): CameraContacts;
    static bufferHasIdentifier(bb: flatbuffers.ByteBuffer): boolean;
    schemaMajor(): number;
    schemaMinor(): number;
    layer(obj?: LayerInfo): LayerInfo | null;
    contacts(index: number, obj?: CameraContact): CameraContact | null;
    contactsLength(): number;
    static startCameraContacts(builder: flatbuffers.Builder): void;
    static addSchemaMajor(builder: flatbuffers.Builder, schemaMajor: number): void;
    static addSchemaMinor(builder: flatbuffers.Builder, schemaMinor: number): void;
    static addLayer(builder: flatbuffers.Builder, layerOffset: flatbuffers.Offset): void;
    static addContacts(builder: flatbuffers.Builder, contactsOffset: flatbuffers.Offset): void;
    static createContactsVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startContactsVector(builder: flatbuffers.Builder, numElems: number): void;
    static endCameraContacts(builder: flatbuffers.Builder): flatbuffers.Offset;
    static finishCameraContactsBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    static finishSizePrefixedCameraContactsBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    unpack(): CameraContactsT;
    unpackTo(_o: CameraContactsT): void;
}
export declare class CameraContactsT implements flatbuffers.IGeneratedObject {
    schemaMajor: number;
    schemaMinor: number;
    layer: LayerInfoT | null;
    contacts: (CameraContactT)[];
    constructor(schemaMajor?: number, schemaMinor?: number, layer?: LayerInfoT | null, contacts?: (CameraContactT)[]);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
