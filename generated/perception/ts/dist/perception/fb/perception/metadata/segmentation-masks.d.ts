import * as flatbuffers from 'flatbuffers';
import { LayerInfo, LayerInfoT } from '../../perception/metadata/layer-info.js';
import { SegmentationMask, SegmentationMaskT } from '../../perception/metadata/segmentation-mask.js';
export declare class SegmentationMasks implements flatbuffers.IUnpackableObject<SegmentationMasksT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): SegmentationMasks;
    static getRootAsSegmentationMasks(bb: flatbuffers.ByteBuffer, obj?: SegmentationMasks): SegmentationMasks;
    static getSizePrefixedRootAsSegmentationMasks(bb: flatbuffers.ByteBuffer, obj?: SegmentationMasks): SegmentationMasks;
    static bufferHasIdentifier(bb: flatbuffers.ByteBuffer): boolean;
    schemaMajor(): number;
    schemaMinor(): number;
    layer(obj?: LayerInfo): LayerInfo | null;
    masks(index: number, obj?: SegmentationMask): SegmentationMask | null;
    masksLength(): number;
    static startSegmentationMasks(builder: flatbuffers.Builder): void;
    static addSchemaMajor(builder: flatbuffers.Builder, schemaMajor: number): void;
    static addSchemaMinor(builder: flatbuffers.Builder, schemaMinor: number): void;
    static addLayer(builder: flatbuffers.Builder, layerOffset: flatbuffers.Offset): void;
    static addMasks(builder: flatbuffers.Builder, masksOffset: flatbuffers.Offset): void;
    static createMasksVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startMasksVector(builder: flatbuffers.Builder, numElems: number): void;
    static endSegmentationMasks(builder: flatbuffers.Builder): flatbuffers.Offset;
    static finishSegmentationMasksBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    static finishSizePrefixedSegmentationMasksBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    unpack(): SegmentationMasksT;
    unpackTo(_o: SegmentationMasksT): void;
}
export declare class SegmentationMasksT implements flatbuffers.IGeneratedObject {
    schemaMajor: number;
    schemaMinor: number;
    layer: LayerInfoT | null;
    masks: (SegmentationMaskT)[];
    constructor(schemaMajor?: number, schemaMinor?: number, layer?: LayerInfoT | null, masks?: (SegmentationMaskT)[]);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
