// Copyright (C) 2026 Arm Limited. All rights reserved.
// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.
var __classPrivateFieldSet = (this && this.__classPrivateFieldSet) || function (receiver, state, value, kind, f) {
    if (kind === "m") throw new TypeError("Private method is not writable");
    if (kind === "a" && !f) throw new TypeError("Private accessor was defined without a setter");
    if (typeof state === "function" ? receiver !== state || !f : !state.has(receiver)) throw new TypeError("Cannot write private member to an object whose class did not declare it");
    return (kind === "a" ? f.call(receiver, value) : f ? f.value = value : state.set(receiver, value)), value;
};
var __classPrivateFieldGet = (this && this.__classPrivateFieldGet) || function (receiver, state, kind, f) {
    if (kind === "a" && !f) throw new TypeError("Private accessor was defined without a getter");
    if (typeof state === "function" ? receiver !== state || !f : !state.has(receiver)) throw new TypeError("Cannot read private member from an object whose class did not declare it");
    return kind === "m" ? f : kind === "a" ? f.call(receiver) : f ? f.value : state.get(receiver);
};
var _ExternalKey_value;
import { Builder, ByteBuffer } from 'flatbuffers';
import { WireEnvelope as FbEnvelope } from './fb/perception/internalfb/wire-envelope.js';
import { WirePayload as Payload } from './fb/perception/internalfb/wire-payload.js';
import { _CLASS_TO_ID, _TYPE_REGISTRY, } from './registry.js';
export const SDK_NAME = 'perception';
export const SDK_VERSION = '0.2.1';
export const SCHEMA_SET_SHA256 = '0ba6dfe959e1453ce12c7a8707623bc15d94d52c9235c26f7e27f31dda0775c5';
export const EXTERNAL_KEY_MIN = BigInt('9223372036854775808');
const EXTERNAL_KEY_MASK = EXTERNAL_KEY_MIN - BigInt(1);
const EXTERNAL_HASH_OFFSET = BigInt('14695981039346656037');
const EXTERNAL_HASH_PRIME = BigInt('1099511628211');
const UINT64_MASK = BigInt('0xffffffffffffffff');
const textEncoder = new TextEncoder();
const externalKeyToken = Symbol('external-key');
const SDK_NAME_PATTERN = /^[a-z][a-z0-9_]*$/;
const SEMANTIC_VERSION_PATTERN = /^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$/;
const SHA256_PATTERN = /^[0-9a-f]{64}$/;
export var ProducerIdentityStatus;
(function (ProducerIdentityStatus) {
    ProducerIdentityStatus["ExactMatch"] = "exact_match";
    ProducerIdentityStatus["Missing"] = "missing";
    ProducerIdentityStatus["Malformed"] = "malformed";
    ProducerIdentityStatus["SdkNameMismatch"] = "sdk_name_mismatch";
    ProducerIdentityStatus["SdkVersionMismatch"] = "sdk_version_mismatch";
    ProducerIdentityStatus["SchemaSetMismatch"] = "schema_set_mismatch";
})(ProducerIdentityStatus || (ProducerIdentityStatus = {}));
function isExternalKeyValue(value) {
    return value >= EXTERNAL_KEY_MIN && value <= UINT64_MASK;
}
export class ExternalKey {
    constructor(value, token) {
        _ExternalKey_value.set(this, void 0);
        if (token !== externalKeyToken || !isExternalKeyValue(value)) {
            throw new Error(`${SDK_NAME} external keys must be created with external_key()`);
        }
        __classPrivateFieldSet(this, _ExternalKey_value, value, "f");
    }
    static fromString(key) {
        if (typeof key !== 'string') {
            throw new Error(`${SDK_NAME} external key must be a string`);
        }
        let value = EXTERNAL_HASH_OFFSET;
        for (const byte of textEncoder.encode(key)) {
            value ^= BigInt(byte);
            value = (value * EXTERNAL_HASH_PRIME) & UINT64_MASK;
        }
        return new ExternalKey((value & EXTERNAL_KEY_MASK) | EXTERNAL_KEY_MIN, externalKeyToken);
    }
    get value() {
        return __classPrivateFieldGet(this, _ExternalKey_value, "f");
    }
    toString() {
        return __classPrivateFieldGet(this, _ExternalKey_value, "f").toString();
    }
}
_ExternalKey_value = new WeakMap();
export function is_external_key(key) {
    return key instanceof ExternalKey && isExternalKeyValue(key.value);
}
export function external_key(key) {
    return ExternalKey.fromString(key);
}
function resolveExternalKey(key) {
    if (!is_external_key(key)) {
        if (typeof key === 'string') {
            throw new Error(`${SDK_NAME} external envelope operations require ExternalKey; call external_key(...) first`);
        }
        throw new Error(`${SDK_NAME} external key must be a value returned by external_key()`);
    }
    return key.value;
}
function resolvePayloadClass(payloadType) {
    if (typeof payloadType !== 'function') {
        throw new Error(`${SDK_NAME} selector must be a generated payload class or ExternalKey`);
    }
    const id = _CLASS_TO_ID.get(payloadType);
    if (id === undefined) {
        throw new Error(`unknown ${SDK_NAME} payload type: ${payloadType.name}`);
    }
    return id;
}
function resolveNativePayload(value) {
    const id = _CLASS_TO_ID.get(value.constructor);
    if (id === undefined) {
        throw new Error(`unknown ${SDK_NAME} payload type: ${value.constructor.name}`);
    }
    return id;
}
function copyBlob(payload) {
    const blobArray = payload.blobArray();
    if (blobArray) {
        return new Uint8Array(blobArray);
    }
    const len = payload.blobLength();
    const out = new Uint8Array(len);
    for (let i = 0; i < len; i += 1) {
        out[i] = payload.blob(i) ?? 0;
    }
    return out;
}
function copyBytes(data) {
    const bytes = data instanceof Uint8Array ? data : new Uint8Array(data);
    return new Uint8Array(bytes);
}
function packNativePayload(id, value) {
    const info = _TYPE_REGISTRY.get(id);
    if (!info) {
        throw new Error(`unknown ${SDK_NAME} payload id: ${id}`);
    }
    const builder = new Builder(1024);
    const offset = value.pack(builder);
    builder.finish(offset, info.file_identifier);
    return builder.asUint8Array();
}
export class Envelope {
    constructor(packet) {
        this.payloadEntries = [];
        this.validEnvelope = true;
        this.errorMessage = null;
        this.producerName = SDK_NAME;
        this.producerVersion = SDK_VERSION;
        this.producerSchemaDigest = SCHEMA_SET_SHA256;
        if (packet !== undefined) {
            this.load(packet);
        }
    }
    load(packet) {
        this.payloadEntries = [];
        this.validEnvelope = false;
        this.errorMessage = null;
        this.producerName = '';
        this.producerVersion = '';
        this.producerSchemaDigest = '';
        const bytes = packet instanceof Uint8Array ? packet : new Uint8Array(packet);
        try {
            const bb = new ByteBuffer(bytes);
            if (!FbEnvelope.bufferHasIdentifier(bb)) {
                this.errorMessage = `invalid ${SDK_NAME} envelope file_identifier`;
                return;
            }
            const envelope = FbEnvelope.getRootAsWireEnvelope(bb);
            this.producerName = envelope.producerSdkName() ?? '';
            this.producerVersion = envelope.producerSdkVersion() ?? '';
            this.producerSchemaDigest = envelope.producerSchemaSetSha256() ?? '';
            const count = envelope.payloadsLength();
            for (let i = 0; i < count; i += 1) {
                const payload = envelope.payloads(i, new Payload());
                if (payload === null) {
                    continue;
                }
                const id = payload.id();
                this.payloadEntries.push({
                    id,
                    blob: copyBlob(payload),
                });
            }
            this.validEnvelope = true;
        }
        catch (error) {
            this.errorMessage = `invalid ${SDK_NAME} envelope: ${String(error)}`;
        }
    }
    // state
    valid() {
        return this.validEnvelope;
    }
    error() {
        return this.errorMessage;
    }
    producerSdkName() {
        return this.producerName;
    }
    producerSdkVersion() {
        return this.producerVersion;
    }
    producerSchemaSetSha256() {
        return this.producerSchemaDigest;
    }
    producerIdentity() {
        if (!this.producerName || !this.producerVersion || !this.producerSchemaDigest) {
            return ProducerIdentityStatus.Missing;
        }
        if (!SDK_NAME_PATTERN.test(this.producerName)
            || !SEMANTIC_VERSION_PATTERN.test(this.producerVersion)
            || !SHA256_PATTERN.test(this.producerSchemaDigest)) {
            return ProducerIdentityStatus.Malformed;
        }
        if (this.producerName !== SDK_NAME)
            return ProducerIdentityStatus.SdkNameMismatch;
        if (this.producerVersion !== SDK_VERSION)
            return ProducerIdentityStatus.SdkVersionMismatch;
        if (this.producerSchemaDigest !== SCHEMA_SET_SHA256) {
            return ProducerIdentityStatus.SchemaSetMismatch;
        }
        return ProducerIdentityStatus.ExactMatch;
    }
    empty() {
        return this.size() === 0;
    }
    size() {
        if (!this.validEnvelope)
            return 0;
        return this.payloadEntries.length;
    }
    count(selector) {
        if (!this.validEnvelope)
            return 0;
        if (is_external_key(selector)) {
            const id = resolveExternalKey(selector);
            return this.payloadEntries.filter((entry) => entry.id === id).length;
        }
        const id = resolvePayloadClass(selector);
        return this.payloadEntries.filter((entry) => {
            if (entry.id !== id)
                return false;
            return this.decodePayloadEntry(id, entry.blob) !== null;
        }).length;
    }
    // query
    contains(selector) {
        return this.count(selector) > 0;
    }
    // access
    decodePayloadEntry(id, blob) {
        const info = _TYPE_REGISTRY.get(id);
        if (!info)
            return null;
        const blobCopy = new Uint8Array(blob);
        if (info.verify && !info.verify(blobCopy))
            return null;
        try {
            return info.decode(blobCopy);
        }
        catch {
            return null;
        }
    }
    valueAtId(id, index) {
        if (!this.validEnvelope)
            return null;
        let seen = 0;
        for (const entry of this.payloadEntries) {
            if (entry.id !== id)
                continue;
            const value = this.decodePayloadEntry(id, entry.blob);
            if (value === null)
                continue;
            if (seen === index)
                return value;
            seen += 1;
        }
        return null;
    }
    externalValueAtId(id, index) {
        if (!this.validEnvelope)
            return null;
        let seen = 0;
        for (const entry of this.payloadEntries) {
            if (entry.id !== id)
                continue;
            if (seen === index)
                return new Uint8Array(entry.blob);
            seen += 1;
        }
        return null;
    }
    get(selector, index = 0) {
        if (is_external_key(selector)) {
            return this.externalValueAtId(resolveExternalKey(selector), index);
        }
        const id = resolvePayloadClass(selector);
        return this.valueAtId(id, index);
    }
    *for_each(selector) {
        if (is_external_key(selector)) {
            const id = resolveExternalKey(selector);
            if (!this.validEnvelope)
                return;
            for (const entry of this.payloadEntries) {
                if (entry.id === id)
                    yield new Uint8Array(entry.blob);
            }
            return;
        }
        const id = resolvePayloadClass(selector);
        for (const entry of this.payloadEntries) {
            if (entry.id !== id)
                continue;
            const value = this.decodePayloadEntry(id, entry.blob);
            if (value !== null)
                yield value;
        }
    }
    add(value, blob) {
        if (!this.validEnvelope) {
            throw new Error(`cannot add to invalid ${SDK_NAME} envelope: ${this.errorMessage ?? 'unknown error'}`);
        }
        if (is_external_key(value)) {
            if (blob === undefined) {
                throw new Error(`${SDK_NAME} external add requires a byte buffer`);
            }
            this.payloadEntries.push({
                id: resolveExternalKey(value),
                blob: copyBytes(blob),
            });
            return;
        }
        if (blob !== undefined) {
            throw new Error(`${SDK_NAME} external add requires ExternalKey; call external_key(...) first`);
        }
        const id = resolveNativePayload(value);
        this.payloadEntries.push({
            id,
            blob: packNativePayload(id, value),
        });
    }
    serialize() {
        if (!this.validEnvelope) {
            throw new Error(`cannot serialize invalid ${SDK_NAME} envelope: ${this.errorMessage ?? 'unknown error'}`);
        }
        const builder = new Builder(1024);
        const payloadOffsets = this.payloadEntries.map((entry) => {
            const blobOffset = Payload.createBlobVector(builder, entry.blob);
            return Payload.createWirePayload(builder, entry.id, blobOffset);
        });
        const payloadsOffset = FbEnvelope.createPayloadsVector(builder, payloadOffsets);
        const producerNameOffset = builder.createString(SDK_NAME);
        const producerVersionOffset = builder.createString(SDK_VERSION);
        const producerSchemaDigestOffset = builder.createString(SCHEMA_SET_SHA256);
        const envelopeOffset = FbEnvelope.createWireEnvelope(builder, payloadsOffset, producerNameOffset, producerVersionOffset, producerSchemaDigestOffset);
        FbEnvelope.finishWireEnvelopeBuffer(builder, envelopeOffset);
        return builder.asUint8Array();
    }
}
