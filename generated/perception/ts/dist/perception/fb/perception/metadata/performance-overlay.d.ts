import * as flatbuffers from 'flatbuffers';
export declare class PerformanceOverlay implements flatbuffers.IUnpackableObject<PerformanceOverlayT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): PerformanceOverlay;
    static getRootAsPerformanceOverlay(bb: flatbuffers.ByteBuffer, obj?: PerformanceOverlay): PerformanceOverlay;
    static getSizePrefixedRootAsPerformanceOverlay(bb: flatbuffers.ByteBuffer, obj?: PerformanceOverlay): PerformanceOverlay;
    static bufferHasIdentifier(bb: flatbuffers.ByteBuffer): boolean;
    schemaMajor(): number;
    schemaMinor(): number;
    lines(index: number): string;
    lines(index: number, optionalEncoding: flatbuffers.Encoding): string | Uint8Array;
    linesLength(): number;
    static startPerformanceOverlay(builder: flatbuffers.Builder): void;
    static addSchemaMajor(builder: flatbuffers.Builder, schemaMajor: number): void;
    static addSchemaMinor(builder: flatbuffers.Builder, schemaMinor: number): void;
    static addLines(builder: flatbuffers.Builder, linesOffset: flatbuffers.Offset): void;
    static createLinesVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startLinesVector(builder: flatbuffers.Builder, numElems: number): void;
    static endPerformanceOverlay(builder: flatbuffers.Builder): flatbuffers.Offset;
    static finishPerformanceOverlayBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    static finishSizePrefixedPerformanceOverlayBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    static createPerformanceOverlay(builder: flatbuffers.Builder, schemaMajor: number, schemaMinor: number, linesOffset: flatbuffers.Offset): flatbuffers.Offset;
    unpack(): PerformanceOverlayT;
    unpackTo(_o: PerformanceOverlayT): void;
}
export declare class PerformanceOverlayT implements flatbuffers.IGeneratedObject {
    schemaMajor: number;
    schemaMinor: number;
    lines: (string)[];
    constructor(schemaMajor?: number, schemaMinor?: number, lines?: (string)[]);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
