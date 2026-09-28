// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.
import * as flatbuffers from 'flatbuffers';
export declare class ProducerInfo implements flatbuffers.IUnpackableObject<ProducerInfoT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): ProducerInfo;
    static getRootAsProducerInfo(bb: flatbuffers.ByteBuffer, obj?: ProducerInfo): ProducerInfo;
    static getSizePrefixedRootAsProducerInfo(bb: flatbuffers.ByteBuffer, obj?: ProducerInfo): ProducerInfo;
    instanceId(): string | null;
    instanceId(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    component(): string | null;
    component(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    implementation(): string | null;
    implementation(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    static startProducerInfo(builder: flatbuffers.Builder): void;
    static addInstanceId(builder: flatbuffers.Builder, instanceIdOffset: flatbuffers.Offset): void;
    static addComponent(builder: flatbuffers.Builder, componentOffset: flatbuffers.Offset): void;
    static addImplementation(builder: flatbuffers.Builder, implementationOffset: flatbuffers.Offset): void;
    static endProducerInfo(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createProducerInfo(builder: flatbuffers.Builder, instanceIdOffset: flatbuffers.Offset, componentOffset: flatbuffers.Offset, implementationOffset: flatbuffers.Offset): flatbuffers.Offset;
    unpack(): ProducerInfoT;
    unpackTo(_o: ProducerInfoT): void;
}
export declare class ProducerInfoT implements flatbuffers.IGeneratedObject {
    instanceId: string | Uint8Array | null;
    component: string | Uint8Array | null;
    implementation: string | Uint8Array | null;
    constructor(instanceId?: string | Uint8Array | null, component?: string | Uint8Array | null, implementation?: string | Uint8Array | null);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
