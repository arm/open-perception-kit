import * as flatbuffers from 'flatbuffers';
import { ObjectMeta, ObjectMetaT } from '../../perception/metadata/object-meta.js';
export declare class AudioFrameContext implements flatbuffers.IUnpackableObject<AudioFrameContextT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): AudioFrameContext;
    static getRootAsAudioFrameContext(bb: flatbuffers.ByteBuffer, obj?: AudioFrameContext): AudioFrameContext;
    static getSizePrefixedRootAsAudioFrameContext(bb: flatbuffers.ByteBuffer, obj?: AudioFrameContext): AudioFrameContext;
    object(obj?: ObjectMeta): ObjectMeta | null;
    originalChannels(): bigint;
    originalFrequency(): bigint;
    originalSampleCount(): bigint;
    cutLeftSampleCount(): bigint;
    cutRightSampleCount(): bigint;
    static startAudioFrameContext(builder: flatbuffers.Builder): void;
    static addObject(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset): void;
    static addOriginalChannels(builder: flatbuffers.Builder, originalChannels: bigint): void;
    static addOriginalFrequency(builder: flatbuffers.Builder, originalFrequency: bigint): void;
    static addOriginalSampleCount(builder: flatbuffers.Builder, originalSampleCount: bigint): void;
    static addCutLeftSampleCount(builder: flatbuffers.Builder, cutLeftSampleCount: bigint): void;
    static addCutRightSampleCount(builder: flatbuffers.Builder, cutRightSampleCount: bigint): void;
    static endAudioFrameContext(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createAudioFrameContext(builder: flatbuffers.Builder, objectOffset: flatbuffers.Offset, originalChannels: bigint, originalFrequency: bigint, originalSampleCount: bigint, cutLeftSampleCount: bigint, cutRightSampleCount: bigint): flatbuffers.Offset;
    unpack(): AudioFrameContextT;
    unpackTo(_o: AudioFrameContextT): void;
}
export declare class AudioFrameContextT implements flatbuffers.IGeneratedObject {
    object: ObjectMetaT | null;
    originalChannels: bigint;
    originalFrequency: bigint;
    originalSampleCount: bigint;
    cutLeftSampleCount: bigint;
    cutRightSampleCount: bigint;
    constructor(object?: ObjectMetaT | null, originalChannels?: bigint, originalFrequency?: bigint, originalSampleCount?: bigint, cutLeftSampleCount?: bigint, cutRightSampleCount?: bigint);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
