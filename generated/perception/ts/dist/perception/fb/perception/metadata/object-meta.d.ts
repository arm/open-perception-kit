import * as flatbuffers from 'flatbuffers';
export declare class ObjectMeta implements flatbuffers.IUnpackableObject<ObjectMetaT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): ObjectMeta;
    static getRootAsObjectMeta(bb: flatbuffers.ByteBuffer, obj?: ObjectMeta): ObjectMeta;
    static getSizePrefixedRootAsObjectMeta(bb: flatbuffers.ByteBuffer, obj?: ObjectMeta): ObjectMeta;
    id(): bigint;
    parentId(): bigint;
    creationTsNs(): bigint;
    static startObjectMeta(builder: flatbuffers.Builder): void;
    static addId(builder: flatbuffers.Builder, id: bigint): void;
    static addParentId(builder: flatbuffers.Builder, parentId: bigint): void;
    static addCreationTsNs(builder: flatbuffers.Builder, creationTsNs: bigint): void;
    static endObjectMeta(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createObjectMeta(builder: flatbuffers.Builder, id: bigint, parentId: bigint, creationTsNs: bigint): flatbuffers.Offset;
    unpack(): ObjectMetaT;
    unpackTo(_o: ObjectMetaT): void;
}
export declare class ObjectMetaT implements flatbuffers.IGeneratedObject {
    id: bigint;
    parentId: bigint;
    creationTsNs: bigint;
    constructor(id?: bigint, parentId?: bigint, creationTsNs?: bigint);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
