// Copyright (C) 2026 Arm Limited. All rights reserved.
// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.
import { ByteBuffer, Encoding } from 'flatbuffers';
import type { IGeneratedObject, IUnpackableObject } from 'flatbuffers';
export type PayloadClass<T = unknown> = Function & {
    prototype: T;
};
export type TypeInfo = {
    name: string;
    root_type: string;
    qualified_root_type: string;
    file_identifier: string;
    decode: (blob: Uint8Array) => unknown;
    verify?: (blob: Uint8Array) => boolean;
};
export declare class _CheckedByteBuffer extends ByteBuffer {
    private remainingDecodeWork;
    constructor(bytes: Uint8Array);
    private requireRange;
    private consumeDecodeWork;
    private relativeTarget;
    readUint8(offset: number): number;
    readUint16(offset: number): number;
    readInt32(offset: number): number;
    __indirect(offset: number): number;
    __vector(offset: number): number;
    __vector_len(offset: number): number;
    __string(offset: number, encoding?: Encoding): string | Uint8Array;
    createScalarList<T>(listAccessor: (index: number) => T | null, listLength: number): T[];
    createObjList<T1 extends IUnpackableObject<T2>, T2 extends IGeneratedObject>(listAccessor: (index: number) => T1 | null, listLength: number): T2[];
}
export declare const _TYPE_REGISTRY: Map<bigint, TypeInfo>;
export declare const _CLASS_TO_ID: Map<PayloadClass<unknown>, bigint>;
