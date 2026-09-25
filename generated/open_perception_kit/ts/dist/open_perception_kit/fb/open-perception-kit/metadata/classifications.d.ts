// Copyright (C) 2026 Arm Limited. All rights reserved.
// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.
import * as flatbuffers from 'flatbuffers';
import { Classification, ClassificationT } from '../../open-perception-kit/metadata/classification.js';
import { LayerInfo, LayerInfoT } from '../../open-perception-kit/metadata/layer-info.js';
import { PersonPresence, PersonPresenceT } from '../../open-perception-kit/metadata/person-presence.js';
export declare class Classifications implements flatbuffers.IUnpackableObject<ClassificationsT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): Classifications;
    static getRootAsClassifications(bb: flatbuffers.ByteBuffer, obj?: Classifications): Classifications;
    static getSizePrefixedRootAsClassifications(bb: flatbuffers.ByteBuffer, obj?: Classifications): Classifications;
    static bufferHasIdentifier(bb: flatbuffers.ByteBuffer): boolean;
    schemaMajor(): number;
    schemaMinor(): number;
    layer(obj?: LayerInfo): LayerInfo | null;
    classifications(index: number, obj?: Classification): Classification | null;
    classificationsLength(): number;
    personPresence(index: number, obj?: PersonPresence): PersonPresence | null;
    personPresenceLength(): number;
    static startClassifications(builder: flatbuffers.Builder): void;
    static addSchemaMajor(builder: flatbuffers.Builder, schemaMajor: number): void;
    static addSchemaMinor(builder: flatbuffers.Builder, schemaMinor: number): void;
    static addLayer(builder: flatbuffers.Builder, layerOffset: flatbuffers.Offset): void;
    static addClassifications(builder: flatbuffers.Builder, classificationsOffset: flatbuffers.Offset): void;
    static createClassificationsVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startClassificationsVector(builder: flatbuffers.Builder, numElems: number): void;
    static addPersonPresence(builder: flatbuffers.Builder, personPresenceOffset: flatbuffers.Offset): void;
    static createPersonPresenceVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startPersonPresenceVector(builder: flatbuffers.Builder, numElems: number): void;
    static endClassifications(builder: flatbuffers.Builder): flatbuffers.Offset;
    static finishClassificationsBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    static finishSizePrefixedClassificationsBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    unpack(): ClassificationsT;
    unpackTo(_o: ClassificationsT): void;
}
export declare class ClassificationsT implements flatbuffers.IGeneratedObject {
    schemaMajor: number;
    schemaMinor: number;
    layer: LayerInfoT | null;
    classifications: (ClassificationT)[];
    personPresence: (PersonPresenceT)[];
    constructor(schemaMajor?: number, schemaMinor?: number, layer?: LayerInfoT | null, classifications?: (ClassificationT)[], personPresence?: (PersonPresenceT)[]);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
