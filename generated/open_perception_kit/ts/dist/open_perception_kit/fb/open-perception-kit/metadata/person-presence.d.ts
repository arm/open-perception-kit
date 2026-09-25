// Copyright (C) 2026 Arm Limited. All rights reserved.
// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.
import * as flatbuffers from 'flatbuffers';
import { ObjectMeta, ObjectMetaT } from '../../open-perception-kit/metadata/object-meta.js';
export declare class PersonPresence implements flatbuffers.IUnpackableObject<PersonPresenceT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): PersonPresence;
    static getRootAsPersonPresence(bb: flatbuffers.ByteBuffer, obj?: PersonPresence): PersonPresence;
    static getSizePrefixedRootAsPersonPresence(bb: flatbuffers.ByteBuffer, obj?: PersonPresence): PersonPresence;
    object(obj?: ObjectMeta): ObjectMeta | null;
    yesConfidence(): number;
    noConfidence(): number;
    static startPersonPresence(builder: flatbuffers.Builder): void;
    static addObject(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset): void;
    static addYesConfidence(builder: flatbuffers.Builder, yesConfidence: number): void;
    static addNoConfidence(builder: flatbuffers.Builder, noConfidence: number): void;
    static endPersonPresence(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createPersonPresence(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset, yesConfidence: number, noConfidence: number): flatbuffers.Offset;
    unpack(): PersonPresenceT;
    unpackTo(_o: PersonPresenceT): void;
}
export declare class PersonPresenceT implements flatbuffers.IGeneratedObject {
    object: ObjectMetaT | null;
    yesConfidence: number;
    noConfidence: number;
    constructor(object?: ObjectMetaT | null, yesConfidence?: number, noConfidence?: number);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
