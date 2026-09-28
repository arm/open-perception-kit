// Copyright (C) 2026 Arm Limited. All rights reserved.
// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.
import { ByteBuffer, Encoding } from 'flatbuffers';
import type { IGeneratedObject, IUnpackableObject } from 'flatbuffers';
import { BoxDetections as BoxDetections_556103652012567315, BoxDetectionsT as BoxDetectionsT_556103652012567315 } from './fb/open-perception-kit/metadata/box-detections.js';
import { Classifications as Classifications_2852697023809600655, ClassificationsT as ClassificationsT_2852697023809600655 } from './fb/open-perception-kit/metadata/classifications.js';
import { FrameContext as FrameContext_2065478860695108412, FrameContextT as FrameContextT_2065478860695108412 } from './fb/open-perception-kit/metadata/frame-context.js';
import { ObjectEmbeddings as ObjectEmbeddings_5972661533224817501, ObjectEmbeddingsT as ObjectEmbeddingsT_5972661533224817501 } from './fb/open-perception-kit/metadata/object-embeddings.js';
import { ObjectTracks as ObjectTracks_2960585987463094496, ObjectTracksT as ObjectTracksT_2960585987463094496 } from './fb/open-perception-kit/metadata/object-tracks.js';
import { PerformanceOverlay as PerformanceOverlay_7749401259036278028, PerformanceOverlayT as PerformanceOverlayT_7749401259036278028 } from './fb/open-perception-kit/metadata/performance-overlay.js';
import { PoseEstimations as PoseEstimations_9114952555105892553, PoseEstimationsT as PoseEstimationsT_9114952555105892553 } from './fb/open-perception-kit/metadata/pose-estimations.js';
import { SegmentationMasks as SegmentationMasks_1998909987238011535, SegmentationMasksT as SegmentationMasksT_1998909987238011535 } from './fb/open-perception-kit/metadata/segmentation-masks.js';
import { TrackTraces as TrackTraces_238211229389337861, TrackTracesT as TrackTracesT_238211229389337861 } from './fb/open-perception-kit/metadata/track-traces.js';
export type PayloadClass<T = unknown> = Function & { prototype: T };

export type TypeInfo = {
  name: string;
  root_type: string;
  qualified_root_type: string;
  file_identifier: string;
  decode: (blob: Uint8Array) => unknown;
  verify?: (blob: Uint8Array) => boolean;
};

export class _CheckedByteBuffer extends ByteBuffer {
  private remainingDecodeWork: number;

  constructor(bytes: Uint8Array) {
    super(bytes);
    this.remainingDecodeWork = bytes.byteLength;
  }

  private requireRange(offset: number, size: number): void {
    if (!Number.isSafeInteger(offset)
        || !Number.isSafeInteger(size)
        || offset < 0
        || size < 0
        || offset > this.capacity() - size) {
      throw new RangeError('FlatBuffer read outside buffer');
    }
  }

  private consumeDecodeWork(size: number): void {
    if (!Number.isSafeInteger(size) || size < 0 || size > this.remainingDecodeWork) {
      throw new RangeError('FlatBuffer decode work exceeds buffer');
    }
    this.remainingDecodeWork -= size;
  }

  private relativeTarget(offset: number, minimumSize: number): number {
    this.requireRange(offset, 4);
    const relative = this.readUint32(offset);
    if (relative < 4) {
      throw new RangeError('invalid FlatBuffer relative offset');
    }
    const target = offset + relative;
    this.requireRange(target, minimumSize);
    return target;
  }

  override readUint8(offset: number): number {
    this.requireRange(offset, 1);
    return super.readUint8(offset);
  }

  override readUint16(offset: number): number {
    this.requireRange(offset, 2);
    return super.readUint16(offset);
  }

  override readInt32(offset: number): number {
    this.requireRange(offset, 4);
    return super.readInt32(offset);
  }

  override __indirect(offset: number): number {
    return this.relativeTarget(offset, 4);
  }

  override __vector(offset: number): number {
    return this.relativeTarget(offset, 4) + 4;
  }

  override __vector_len(offset: number): number {
    const data = this.__vector(offset);
    const length = this.readInt32(data - 4);
    if (length < 0 || length > this.capacity() - data) {
      throw new RangeError('invalid FlatBuffer vector length');
    }
    return length;
  }

  override __string(offset: number, encoding?: Encoding): string | Uint8Array {
    const start = this.relativeTarget(offset, 4);
    const length = this.readInt32(start);
    if (length < 0) {
      throw new RangeError('invalid FlatBuffer string length');
    }
    this.requireRange(start + 4, length);
    this.consumeDecodeWork(length);
    return super.__string(offset, encoding);
  }

  override createScalarList<T>(
    listAccessor: (index: number) => T | null,
    listLength: number,
  ): T[] {
    this.consumeDecodeWork(listLength);
    return super.createScalarList(listAccessor, listLength);
  }

  override createObjList<T1 extends IUnpackableObject<T2>, T2 extends IGeneratedObject>(
    listAccessor: (index: number) => T1 | null,
    listLength: number,
  ): T2[] {
    this.consumeDecodeWork(listLength);
    return super.createObjList(listAccessor, listLength);
  }
}

const decode_556103652012567315 = (blob: Uint8Array): unknown => BoxDetections_556103652012567315.getRootAsBoxDetections(new _CheckedByteBuffer(blob)).unpack();
const verify_556103652012567315 = (blob: Uint8Array): boolean => BoxDetections_556103652012567315.bufferHasIdentifier(new _CheckedByteBuffer(blob));
const decode_2852697023809600655 = (blob: Uint8Array): unknown => Classifications_2852697023809600655.getRootAsClassifications(new _CheckedByteBuffer(blob)).unpack();
const verify_2852697023809600655 = (blob: Uint8Array): boolean => Classifications_2852697023809600655.bufferHasIdentifier(new _CheckedByteBuffer(blob));
const decode_2065478860695108412 = (blob: Uint8Array): unknown => FrameContext_2065478860695108412.getRootAsFrameContext(new _CheckedByteBuffer(blob)).unpack();
const verify_2065478860695108412 = (blob: Uint8Array): boolean => FrameContext_2065478860695108412.bufferHasIdentifier(new _CheckedByteBuffer(blob));
const decode_5972661533224817501 = (blob: Uint8Array): unknown => ObjectEmbeddings_5972661533224817501.getRootAsObjectEmbeddings(new _CheckedByteBuffer(blob)).unpack();
const verify_5972661533224817501 = (blob: Uint8Array): boolean => ObjectEmbeddings_5972661533224817501.bufferHasIdentifier(new _CheckedByteBuffer(blob));
const decode_2960585987463094496 = (blob: Uint8Array): unknown => ObjectTracks_2960585987463094496.getRootAsObjectTracks(new _CheckedByteBuffer(blob)).unpack();
const verify_2960585987463094496 = (blob: Uint8Array): boolean => ObjectTracks_2960585987463094496.bufferHasIdentifier(new _CheckedByteBuffer(blob));
const decode_7749401259036278028 = (blob: Uint8Array): unknown => PerformanceOverlay_7749401259036278028.getRootAsPerformanceOverlay(new _CheckedByteBuffer(blob)).unpack();
const verify_7749401259036278028 = (blob: Uint8Array): boolean => PerformanceOverlay_7749401259036278028.bufferHasIdentifier(new _CheckedByteBuffer(blob));
const decode_9114952555105892553 = (blob: Uint8Array): unknown => PoseEstimations_9114952555105892553.getRootAsPoseEstimations(new _CheckedByteBuffer(blob)).unpack();
const verify_9114952555105892553 = (blob: Uint8Array): boolean => PoseEstimations_9114952555105892553.bufferHasIdentifier(new _CheckedByteBuffer(blob));
const decode_1998909987238011535 = (blob: Uint8Array): unknown => SegmentationMasks_1998909987238011535.getRootAsSegmentationMasks(new _CheckedByteBuffer(blob)).unpack();
const verify_1998909987238011535 = (blob: Uint8Array): boolean => SegmentationMasks_1998909987238011535.bufferHasIdentifier(new _CheckedByteBuffer(blob));
const decode_238211229389337861 = (blob: Uint8Array): unknown => TrackTraces_238211229389337861.getRootAsTrackTraces(new _CheckedByteBuffer(blob)).unpack();
const verify_238211229389337861 = (blob: Uint8Array): boolean => TrackTraces_238211229389337861.bufferHasIdentifier(new _CheckedByteBuffer(blob));

export const _TYPE_REGISTRY = new Map<bigint, TypeInfo>([
  [556103652012567315n, {
    name: 'open_perception_kit::metadata::BoxDetections',
    root_type: 'BoxDetections',
    qualified_root_type: 'open_perception_kit.metadata.BoxDetections',
    file_identifier: 'BDET',
    decode: decode_556103652012567315,
    verify: verify_556103652012567315,
  }],
  [2852697023809600655n, {
    name: 'open_perception_kit::metadata::Classifications',
    root_type: 'Classifications',
    qualified_root_type: 'open_perception_kit.metadata.Classifications',
    file_identifier: 'CLSF',
    decode: decode_2852697023809600655,
    verify: verify_2852697023809600655,
  }],
  [2065478860695108412n, {
    name: 'open_perception_kit::metadata::FrameContext',
    root_type: 'FrameContext',
    qualified_root_type: 'open_perception_kit.metadata.FrameContext',
    file_identifier: 'FCTX',
    decode: decode_2065478860695108412,
    verify: verify_2065478860695108412,
  }],
  [5972661533224817501n, {
    name: 'open_perception_kit::metadata::ObjectEmbeddings',
    root_type: 'ObjectEmbeddings',
    qualified_root_type: 'open_perception_kit.metadata.ObjectEmbeddings',
    file_identifier: 'EMBE',
    decode: decode_5972661533224817501,
    verify: verify_5972661533224817501,
  }],
  [2960585987463094496n, {
    name: 'open_perception_kit::metadata::ObjectTracks',
    root_type: 'ObjectTracks',
    qualified_root_type: 'open_perception_kit.metadata.ObjectTracks',
    file_identifier: 'TRKS',
    decode: decode_2960585987463094496,
    verify: verify_2960585987463094496,
  }],
  [7749401259036278028n, {
    name: 'open_perception_kit::metadata::PerformanceOverlay',
    root_type: 'PerformanceOverlay',
    qualified_root_type: 'open_perception_kit.metadata.PerformanceOverlay',
    file_identifier: 'PERF',
    decode: decode_7749401259036278028,
    verify: verify_7749401259036278028,
  }],
  [9114952555105892553n, {
    name: 'open_perception_kit::metadata::PoseEstimations',
    root_type: 'PoseEstimations',
    qualified_root_type: 'open_perception_kit.metadata.PoseEstimations',
    file_identifier: 'POSE',
    decode: decode_9114952555105892553,
    verify: verify_9114952555105892553,
  }],
  [1998909987238011535n, {
    name: 'open_perception_kit::metadata::SegmentationMasks',
    root_type: 'SegmentationMasks',
    qualified_root_type: 'open_perception_kit.metadata.SegmentationMasks',
    file_identifier: 'SGMS',
    decode: decode_1998909987238011535,
    verify: verify_1998909987238011535,
  }],
  [238211229389337861n, {
    name: 'open_perception_kit::metadata::TrackTraces',
    root_type: 'TrackTraces',
    qualified_root_type: 'open_perception_kit.metadata.TrackTraces',
    file_identifier: 'TRCE',
    decode: decode_238211229389337861,
    verify: verify_238211229389337861,
  }],
]);

export const _CLASS_TO_ID = new Map<PayloadClass, bigint>([
  [BoxDetectionsT_556103652012567315, 556103652012567315n],
  [ClassificationsT_2852697023809600655, 2852697023809600655n],
  [FrameContextT_2065478860695108412, 2065478860695108412n],
  [ObjectEmbeddingsT_5972661533224817501, 5972661533224817501n],
  [ObjectTracksT_2960585987463094496, 2960585987463094496n],
  [PerformanceOverlayT_7749401259036278028, 7749401259036278028n],
  [PoseEstimationsT_9114952555105892553, 9114952555105892553n],
  [SegmentationMasksT_1998909987238011535, 1998909987238011535n],
  [TrackTracesT_238211229389337861, 238211229389337861n],
]);