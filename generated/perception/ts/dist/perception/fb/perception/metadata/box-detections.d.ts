import * as flatbuffers from 'flatbuffers';
import { BoxDetection, BoxDetectionT } from '../../perception/metadata/box-detection.js';
import { LayerInfo, LayerInfoT } from '../../perception/metadata/layer-info.js';
export declare class BoxDetections implements flatbuffers.IUnpackableObject<BoxDetectionsT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): BoxDetections;
    static getRootAsBoxDetections(bb: flatbuffers.ByteBuffer, obj?: BoxDetections): BoxDetections;
    static getSizePrefixedRootAsBoxDetections(bb: flatbuffers.ByteBuffer, obj?: BoxDetections): BoxDetections;
    static bufferHasIdentifier(bb: flatbuffers.ByteBuffer): boolean;
    schemaMajor(): number;
    schemaMinor(): number;
    layer(obj?: LayerInfo): LayerInfo | null;
    detections(index: number, obj?: BoxDetection): BoxDetection | null;
    detectionsLength(): number;
    static startBoxDetections(builder: flatbuffers.Builder): void;
    static addSchemaMajor(builder: flatbuffers.Builder, schemaMajor: number): void;
    static addSchemaMinor(builder: flatbuffers.Builder, schemaMinor: number): void;
    static addLayer(builder: flatbuffers.Builder, layerOffset: flatbuffers.Offset): void;
    static addDetections(builder: flatbuffers.Builder, detectionsOffset: flatbuffers.Offset): void;
    static createDetectionsVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startDetectionsVector(builder: flatbuffers.Builder, numElems: number): void;
    static endBoxDetections(builder: flatbuffers.Builder): flatbuffers.Offset;
    static finishBoxDetectionsBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    static finishSizePrefixedBoxDetectionsBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    unpack(): BoxDetectionsT;
    unpackTo(_o: BoxDetectionsT): void;
}
export declare class BoxDetectionsT implements flatbuffers.IGeneratedObject {
    schemaMajor: number;
    schemaMinor: number;
    layer: LayerInfoT | null;
    detections: (BoxDetectionT)[];
    constructor(schemaMajor?: number, schemaMinor?: number, layer?: LayerInfoT | null, detections?: (BoxDetectionT)[]);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
