// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.
import * as flatbuffers from 'flatbuffers';
import { LayerInfo, LayerInfoT } from '../../open-perception-kit/metadata/layer-info.js';
import { ObjectTrack, ObjectTrackT } from '../../open-perception-kit/metadata/object-track.js';
export declare class ObjectTracks implements flatbuffers.IUnpackableObject<ObjectTracksT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): ObjectTracks;
    static getRootAsObjectTracks(bb: flatbuffers.ByteBuffer, obj?: ObjectTracks): ObjectTracks;
    static getSizePrefixedRootAsObjectTracks(bb: flatbuffers.ByteBuffer, obj?: ObjectTracks): ObjectTracks;
    static bufferHasIdentifier(bb: flatbuffers.ByteBuffer): boolean;
    schemaMajor(): number;
    schemaMinor(): number;
    layer(obj?: LayerInfo): LayerInfo | null;
    tracks(index: number, obj?: ObjectTrack): ObjectTrack | null;
    tracksLength(): number;
    static startObjectTracks(builder: flatbuffers.Builder): void;
    static addSchemaMajor(builder: flatbuffers.Builder, schemaMajor: number): void;
    static addSchemaMinor(builder: flatbuffers.Builder, schemaMinor: number): void;
    static addLayer(builder: flatbuffers.Builder, layerOffset: flatbuffers.Offset): void;
    static addTracks(builder: flatbuffers.Builder, tracksOffset: flatbuffers.Offset): void;
    static createTracksVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startTracksVector(builder: flatbuffers.Builder, numElems: number): void;
    static endObjectTracks(builder: flatbuffers.Builder): flatbuffers.Offset;
    static finishObjectTracksBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    static finishSizePrefixedObjectTracksBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    unpack(): ObjectTracksT;
    unpackTo(_o: ObjectTracksT): void;
}
export declare class ObjectTracksT implements flatbuffers.IGeneratedObject {
    schemaMajor: number;
    schemaMinor: number;
    layer: LayerInfoT | null;
    tracks: (ObjectTrackT)[];
    constructor(schemaMajor?: number, schemaMinor?: number, layer?: LayerInfoT | null, tracks?: (ObjectTrackT)[]);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
