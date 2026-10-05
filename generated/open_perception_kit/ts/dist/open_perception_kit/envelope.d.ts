/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.
import { Builder } from 'flatbuffers';
import { PayloadClass } from './registry.js';
type NativePayload = {
    pack(builder: Builder): number;
    constructor: Function;
};
export declare const SDK_NAME = "open_perception_kit";
export declare const SDK_VERSION = "0.1.1";
export declare const SCHEMA_SET_SHA256 = "1b19418d8a0d34038a3c99895fa93a1140c25af910f6bc63c0e11978e12c2876";
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
