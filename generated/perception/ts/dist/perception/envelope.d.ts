// Copyright (C) 2026 Arm Limited. All rights reserved.
// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.
import { Builder } from 'flatbuffers';
import { PayloadClass } from './registry.js';
type NativePayload = {
    pack(builder: Builder): number;
    constructor: Function;
};
export declare const SDK_NAME = "perception";
export declare const SDK_VERSION = "0.2.1";
export declare const SCHEMA_SET_SHA256 = "0ba6dfe959e1453ce12c7a8707623bc15d94d52c9235c26f7e27f31dda0775c5";
export declare const EXTERNAL_KEY_MIN: bigint;
export declare enum ProducerIdentityStatus {
    ExactMatch = "exact_match",
    Missing = "missing",
    Malformed = "malformed",
    SdkNameMismatch = "sdk_name_mismatch",
    SdkVersionMismatch = "sdk_version_mismatch",
    SchemaSetMismatch = "schema_set_mismatch"
}
export declare class ExternalKey {
    #private;
    private constructor();
    static fromString(key: string): ExternalKey;
    get value(): bigint;
    toString(): string;
}
export declare function is_external_key(key: unknown): key is ExternalKey;
export declare function external_key(key: string): ExternalKey;
export declare class Envelope {
    private payloadEntries;
    private validEnvelope;
    private errorMessage;
    private producerName;
    private producerVersion;
    private producerSchemaDigest;
    constructor(packet?: Uint8Array | ArrayBuffer);
    private load;
    valid(): boolean;
    error(): string | null;
    producerSdkName(): string;
    producerSdkVersion(): string;
    producerSchemaSetSha256(): string;
    producerIdentity(): ProducerIdentityStatus;
    empty(): boolean;
    size(): number;
    count<T>(selector: PayloadClass<T> | ExternalKey): number;
    contains<T>(selector: PayloadClass<T> | ExternalKey): boolean;
    private decodePayloadEntry;
    private valueAtId;
    private externalValueAtId;
    get<T>(payloadType: PayloadClass<T>, index?: number): T | null;
    get(key: ExternalKey, index?: number): Uint8Array | null;
    for_each<T>(payloadType: PayloadClass<T>): IterableIterator<T>;
    for_each(key: ExternalKey): IterableIterator<Uint8Array>;
    add(value: NativePayload): void;
    add(key: ExternalKey, blob: Uint8Array | ArrayBuffer): void;
    serialize(): Uint8Array;
}
export {};
