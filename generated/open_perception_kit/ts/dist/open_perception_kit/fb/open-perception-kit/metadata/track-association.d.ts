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
import * as flatbuffers from 'flatbuffers';
import { TrackAssociationKind } from '../../open-perception-kit/metadata/track-association-kind.js';
export declare class TrackAssociation implements flatbuffers.IUnpackableObject<TrackAssociationT> {
    bb: flatbuffers.ByteBuffer | null;
    bb_pos: number;
    __init(i: number, bb: flatbuffers.ByteBuffer): TrackAssociation;
    static getRootAsTrackAssociation(bb: flatbuffers.ByteBuffer, obj?: TrackAssociation): TrackAssociation;
    static getSizePrefixedRootAsTrackAssociation(bb: flatbuffers.ByteBuffer, obj?: TrackAssociation): TrackAssociation;
    kind(): TrackAssociationKind;
    score(): number | null;
    static startTrackAssociation(builder: flatbuffers.Builder): void;
    static addKind(builder: flatbuffers.Builder, kind: TrackAssociationKind): void;
    static addScore(builder: flatbuffers.Builder, score: number): void;
    static endTrackAssociation(builder: flatbuffers.Builder): flatbuffers.Offset;
    static createTrackAssociation(builder: flatbuffers.Builder, kind: TrackAssociationKind, score: number | null): flatbuffers.Offset;
    unpack(): TrackAssociationT;
    unpackTo(_o: TrackAssociationT): void;
}
export declare class TrackAssociationT implements flatbuffers.IGeneratedObject {
    kind: TrackAssociationKind;
    score: number | null;
    constructor(kind?: TrackAssociationKind, score?: number | null);
    pack(builder: flatbuffers.Builder): flatbuffers.Offset;
}
