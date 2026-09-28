/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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
import * as flatbuffers from 'flatbuffers';
import { ProducerInfo, ProducerInfoT } from '../../open-perception-kit/metadata/producer-info.js';
export declare class LayerInfo implements flatbuffers.IUnpackableObject<LayerInfoT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): LayerInfo;
    static getRootAsLayerInfo(bb: flatbuffers.ByteBuffer, obj?: LayerInfo): LayerInfo;
    static getSizePrefixedRootAsLayerInfo(bb: flatbuffers.ByteBuffer, obj?: LayerInfo): LayerInfo;
    engine(): string | null;
    engine(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    model(): string | null;
    model(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    tags(): string | null;
    tags(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    inferElementId(): string | null;
    inferElementId(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    labelFamily(): string | null;
    labelFamily(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    contentType(): string | null;
    contentType(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    compositingMode(): string | null;
    compositingMode(optionalEncoding: flatbuffers.Encoding): string | Uint8Array | null;
    producer(obj?: ProducerInfo): ProducerInfo | null;
    static startLayerInfo(builder: flatbuffers.Builder): void;
    static addEngine(builder: flatbuffers.Builder, engineOffset: flatbuffers.Offset): void;
    static addModel(builder: flatbuffers.Builder, modelOffset: flatbuffers.Offset): void;
    static addTags(builder: flatbuffers.Builder, tagsOffset: flatbuffers.Offset): void;
    static addInferElementId(builder: flatbuffers.Builder, inferElementIdOffset: flatbuffers.Offset): void;
    static addLabelFamily(builder: flatbuffers.Builder, labelFamilyOffset: flatbuffers.Offset): void;
    static addContentType(builder: flatbuffers.Builder, contentTypeOffset: flatbuffers.Offset): void;
    static addCompositingMode(builder: flatbuffers.Builder, compositingModeOffset: flatbuffers.Offset): void;
    static addProducer(builder: flatbuffers.Builder, producerOffset: flatbuffers.Offset): void;
    static endLayerInfo(builder: flatbuffers.Builder): flatbuffers.Offset;
    unpack(): LayerInfoT;
    unpackTo(_o: LayerInfoT): void;
}
export declare class LayerInfoT implements flatbuffers.IGeneratedObject {
    engine: string | Uint8Array | null;
    model: string | Uint8Array | null;
    tags: string | Uint8Array | null;
    inferElementId: string | Uint8Array | null;
    labelFamily: string | Uint8Array | null;
    contentType: string | Uint8Array | null;
    compositingMode: string | Uint8Array | null;
    producer: ProducerInfoT | null;
    constructor(engine?: string | Uint8Array | null, model?: string | Uint8Array | null, tags?: string | Uint8Array | null, inferElementId?: string | Uint8Array | null, labelFamily?: string | Uint8Array | null, contentType?: string | Uint8Array | null, compositingMode?: string | Uint8Array | null, producer?: ProducerInfoT | null);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
