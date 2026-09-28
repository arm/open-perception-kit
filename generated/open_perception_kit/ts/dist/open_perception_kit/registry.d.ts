// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.
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
export declare const _TYPE_REGISTRY: Map<bigint, TypeInfo>;
export declare const _CLASS_TO_ID: Map<PayloadClass<unknown>, bigint>;
