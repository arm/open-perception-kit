// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.
import * as flatbuffers from 'flatbuffers';
import { LayerInfo, LayerInfoT } from '../../open-perception-kit/metadata/layer-info.js';
import { PoseEstimation, PoseEstimationT } from '../../open-perception-kit/metadata/pose-estimation.js';
export declare class PoseEstimations implements flatbuffers.IUnpackableObject<PoseEstimationsT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): PoseEstimations;
    static getRootAsPoseEstimations(bb: flatbuffers.ByteBuffer, obj?: PoseEstimations): PoseEstimations;
    static getSizePrefixedRootAsPoseEstimations(bb: flatbuffers.ByteBuffer, obj?: PoseEstimations): PoseEstimations;
    static bufferHasIdentifier(bb: flatbuffers.ByteBuffer): boolean;
    schemaMajor(): number;
    schemaMinor(): number;
    layer(obj?: LayerInfo): LayerInfo | null;
    poses(index: number, obj?: PoseEstimation): PoseEstimation | null;
    posesLength(): number;
    static startPoseEstimations(builder: flatbuffers.Builder): void;
    static addSchemaMajor(builder: flatbuffers.Builder, schemaMajor: number): void;
    static addSchemaMinor(builder: flatbuffers.Builder, schemaMinor: number): void;
    static addLayer(builder: flatbuffers.Builder, layerOffset: flatbuffers.Offset): void;
    static addPoses(builder: flatbuffers.Builder, posesOffset: flatbuffers.Offset): void;
    static createPosesVector(builder: flatbuffers.Builder, data: flatbuffers.Offset[]): flatbuffers.Offset;
    static startPosesVector(builder: flatbuffers.Builder, numElems: number): void;
    static endPoseEstimations(builder: flatbuffers.Builder): flatbuffers.Offset;
    static finishPoseEstimationsBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    static finishSizePrefixedPoseEstimationsBuffer(builder: flatbuffers.Builder, offset: flatbuffers.Offset): void;
    unpack(): PoseEstimationsT;
    unpackTo(_o: PoseEstimationsT): void;
}
export declare class PoseEstimationsT implements flatbuffers.IGeneratedObject {
    schemaMajor: number;
    schemaMinor: number;
    layer: LayerInfoT | null;
    poses: (PoseEstimationT)[];
    constructor(schemaMajor?: number, schemaMinor?: number, layer?: LayerInfoT | null, poses?: (PoseEstimationT)[]);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
