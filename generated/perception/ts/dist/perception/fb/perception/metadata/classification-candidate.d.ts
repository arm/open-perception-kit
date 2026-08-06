import * as flatbuffers from 'flatbuffers';
export declare class ClassificationCandidate implements flatbuffers.IUnpackableObject<ClassificationCandidateT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): ClassificationCandidate;
    static getRootAsClassificationCandidate(bb: flatbuffers.ByteBuffer, obj?: ClassificationCandidate): ClassificationCandidate;
    static getSizePrefixedRootAsClassificationCandidate(bb: flatbuffers.ByteBuffer, obj?: ClassificationCandidate): ClassificationCandidate;
    confidence(): number;
    classId(): number;
    text(): string | null;
    text(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    x(): number;
    y(): number;
    w(): number;
    h(): number;
    static startClassificationCandidate(builder: flatbuffers.Builder): void;
    static addConfidence(builder: flatbuffers.Builder, confidence: number): void;
    static addClassId(builder: flatbuffers.Builder, classId: number): void;
    static addText(builder: flatbuffers.Builder, textOffset: flatbuffers.Offset): void;
    static addX(builder: flatbuffers.Builder, x: number): void;
    static addY(builder: flatbuffers.Builder, y: number): void;
    static addW(builder: flatbuffers.Builder, w: number): void;
    static addH(builder: flatbuffers.Builder, h: number): void;
    static endClassificationCandidate(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createClassificationCandidate(builder: flatbuffers.Builder, confidence: number, classId: number, textOffset: flatbuffers.Offset, x: number, y: number, w: number, h: number): flatbuffers.Offset;
    unpack(): ClassificationCandidateT;
    unpackTo(_o: ClassificationCandidateT): void;
}
export declare class ClassificationCandidateT implements flatbuffers.IGeneratedObject {
    confidence: number;
    classId: number;
    text: string | Uint8Array | null;
    x: number;
    y: number;
    w: number;
    h: number;
    constructor(confidence?: number, classId?: number, text?: string | Uint8Array | null, x?: number, y?: number, w?: number, h?: number);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
