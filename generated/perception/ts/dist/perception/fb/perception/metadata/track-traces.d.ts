import * as flatbuffers from 'flatbuffers';
import { LayerInfo, LayerInfoT } from '../../perception/metadata/layer-info.js';
import { TrackTrace, TrackTraceT } from '../../perception/metadata/track-trace.js';
export declare class TrackTraces implements flatbuffers.IUnpackableObject<TrackTracesT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): TrackTraces;
    static getRootAsTrackTraces(bb: flatbuffers.ByteBuffer, obj?: TrackTraces): TrackTraces;
    static getSizePrefixedRootAsTrackTraces(bb: flatbuffers.ByteBuffer, obj?: TrackTraces): TrackTraces;
    static bufferHasIdentifier(bb: flatbuffers.ByteBuffer): boolean;
    schemaMajor(): number;
    schemaMinor(): number;
    layer(obj?: LayerInfo): LayerInfo | null;
    traces(index: number, obj?: TrackTrace): TrackTrace | null;
    tracesLength(): number;
    static startTrackTraces(builder: flatbuffers.Builder): void;
    static addSchemaMajor(builder: flatbuffers.Builder, schemaMajor: number): void;
    static addSchemaMinor(builder: flatbuffers.Builder, schemaMinor: number): void;
    static addLayer(builder: flatbuffers.Builder, layerOffset: flatbuffers.Offset): void;
    static addTraces(builder: flatbuffers.Builder, tracesOffset: flatbuffers.Offset): void;
    static createTracesVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startTracesVector(builder: flatbuffers.Builder, numElems: number): void;
    static endTrackTraces(builder: flatbuffers.Builder): flatbuffers.Offset;
    static finishTrackTracesBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    static finishSizePrefixedTrackTracesBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    unpack(): TrackTracesT;
    unpackTo(_o: TrackTracesT): void;
}
export declare class TrackTracesT implements flatbuffers.IGeneratedObject {
    schemaMajor: number;
    schemaMinor: number;
    layer: LayerInfoT | null;
    traces: (TrackTraceT)[];
    constructor(schemaMajor?: number, schemaMinor?: number, layer?: LayerInfoT | null, traces?: (TrackTraceT)[]);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
