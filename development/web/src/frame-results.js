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

import {Envelope, ProducerIdentityStatus} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/index.js';
import {BoxDetectionsT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/box-detections.js';
import {ClassificationsT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/classifications.js';
import {FrameContextT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/frame-context.js';
import {ObjectEmbeddingsT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/object-embeddings.js';
import {ObjectTracksT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/object-tracks.js';
import {PerformanceOverlayT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/performance-overlay.js';
import {PoseEstimationsT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/pose-estimations.js';
import {SegmentationMasksT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/segmentation-masks.js';
import {SemanticSegmentationMasksT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/semantic-segmentation-masks.js';
import {TrackTracesT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/track-traces.js';

export const FRAME_RESULTS_ENCODING = 'perception-frame-results+base64';

export class FrameResultsDecodeError extends Error {}

function text(value) {
    if (value === null || value === undefined) return '';
    if (value instanceof Uint8Array) return new TextDecoder().decode(value);
    return String(value);
}

function identifier(value) {
    return typeof value === 'bigint' ? value.toString() : String(value || 0);
}

function objectData(object) {
    return {
        uuid: identifier(object?.id),
        parentUuid: identifier(object?.parentId),
        creationTsNs: identifier(object?.creationTsNs),
    };
}

function layerData(layer) {
    const producer = layer?.producer;
    return {
        engine: text(layer?.engine),
        model: text(layer?.model),
        tags: text(layer?.tags),
        inferElementId: text(layer?.inferElementId),
        labelFamily: text(layer?.labelFamily),
        contentType: text(layer?.contentType),
        compositingMode: text(layer?.compositingMode),
        producer: producer ? {
            instanceId: text(producer.instanceId),
            component: text(producer.component),
            implementation: text(producer.implementation),
        } : null,
    };
}

function layer(payload, payloadType, detections) {
    return {
        ...layerData(payload.layer),
        payloadType,
        payload,
        count: detections.length,
        detections,
    };
}

function boxData(item) {
    return {
        ...objectData(item.object),
        x: Number(item.box?.x || 0),
        y: Number(item.box?.y || 0),
        width: Number(item.box?.width || 0),
        height: Number(item.box?.height || 0),
        confidence: Number(item.confidence || 0),
        classId: Number(item.classId ?? -1),
        text: text(item.text),
    };
}

function candidateData(candidate) {
    return {
        confidence: Number(candidate.confidence || 0),
        classId: Number(candidate.classId ?? -1),
        text: text(candidate.text),
        x: Number(candidate.x || 0),
        y: Number(candidate.y || 0),
        w: Number(candidate.w || 0),
        h: Number(candidate.h || 0),
    };
}

function addPayloadLayers(envelope, payloadType, name, mapper, layers) {
    for (const payload of envelope.for_each(payloadType)) {
        layers.push(layer(payload, name, mapper(payload)));
    }
}

function decodeBase64(value) {
    if (typeof value !== 'string' || !value || value.length % 4 !== 0) {
        throw new FrameResultsDecodeError('missing or invalid frame_results_packet_b64');
    }
    if (!/^[A-Za-z0-9+/]*={0,2}$/.test(value)) {
        throw new FrameResultsDecodeError('invalid frame_results_packet_b64');
    }
    let decoded;
    try {
        decoded = atob(value);
    } catch (error) {
        throw new FrameResultsDecodeError('invalid frame_results_packet_b64', {cause: error});
    }
    return Uint8Array.from(decoded, (character) => character.charCodeAt(0));
}

export function decodeFrameResultsMessage(message) {
    if (message?.frame_results_encoding !== FRAME_RESULTS_ENCODING) {
        throw new FrameResultsDecodeError(
            `unsupported frame_results_encoding: ${String(message?.frame_results_encoding)}`,
        );
    }

    const envelope = new Envelope(decodeBase64(message.frame_results_packet_b64));
    if (!envelope.valid()) {
        throw new FrameResultsDecodeError(
            `invalid frame results packet: ${envelope.error() || 'unknown error'}`,
        );
    }
    const producerIdentity = envelope.producerIdentity();
    if (producerIdentity !== ProducerIdentityStatus.ExactMatch) {
        throw new FrameResultsDecodeError(
            `incompatible frame results producer identity: ${producerIdentity}`,
        );
    }

    const layers = [];
    const perfdata = [];

    addPayloadLayers(envelope, FrameContextT, 'FrameContext', (payload) => {
        const detections = [];
        if (payload.video) {
            detections.push({
                type: 'VideoFrame',
                data: {
                    ...objectData(payload.video.object),
                    originalWidth: Number(payload.video.originalWidth),
                    originalHeight: Number(payload.video.originalHeight),
                    sourceCropLeft: Number(payload.video.sourceCropLeft),
                    sourceCropRight: Number(payload.video.sourceCropRight),
                    sourceCropTop: Number(payload.video.sourceCropTop),
                    sourceCropBottom: Number(payload.video.sourceCropBottom),
                    letterboxLeft: Number(payload.video.letterboxLeft),
                    letterboxRight: Number(payload.video.letterboxRight),
                    letterboxTop: Number(payload.video.letterboxTop),
                    letterboxBottom: Number(payload.video.letterboxBottom),
                },
            });
        }
        return detections;
    }, layers);

    addPayloadLayers(envelope, BoxDetectionsT, 'BoxDetections', (payload) =>
        payload.detections.map((item) => ({type: 'Rect', data: boxData(item)})), layers);

    addPayloadLayers(envelope, ObjectTracksT, 'ObjectTracks', (payload) =>
        payload.tracks.map((item) => ({
            type: 'Rect',
            data: {
                ...boxData(item),
                sourceId: identifier(item.sourceId),
                trackId: identifier(item.trackId),
                diagnostic: text(item.diagnostic),
                predictedOnly: Boolean(item.predictedOnly),
            },
        })), layers);

    addPayloadLayers(envelope, ClassificationsT, 'Classifications', (payload) => [
        ...payload.classifications.map((item) => ({
            type: 'Classification',
            data: {
                ...objectData(item.object),
                candidates: item.candidates.map(candidateData),
            },
        })),
        ...payload.personPresence.map((item) => ({
            type: 'PersonClassification',
            data: {
                ...objectData(item.object),
                yesConfidence: Number(item.yesConfidence || 0),
                noConfidence: Number(item.noConfidence || 0),
            },
        })),
    ], layers);

    addPayloadLayers(envelope, PoseEstimationsT, 'PoseEstimations', (payload) =>
        payload.poses.map((item) => ({
            type: 'YawPitch',
            data: {
                ...objectData(item.object),
                confidence: Number(item.confidence || 0),
                yaw: Number(item.yaw || 0),
                pitch: Number(item.pitch || 0),
            },
        })), layers);

    addPayloadLayers(envelope, TrackTracesT, 'TrackTraces', (payload) =>
        payload.traces.map((item) => ({
            type: 'TrackTrace',
            data: {
                ...objectData(item.object),
                trackId: identifier(item.trackId),
                points: item.points.map((point) => ({x: Number(point.x), y: Number(point.y)})),
            },
        })), layers);

    addPayloadLayers(envelope, SegmentationMasksT, 'SegmentationMasks', (payload) =>
        payload.masks.map((item) => ({
            type: 'SegmentationMask',
            data: {
                ...objectData(item.object),
                width: Number(item.bitmap?.width || 0),
                height: Number(item.bitmap?.height || 0),
                valueType: text(item.bitmap?.valueType),
                pixels: item.bitmap ? Uint8Array.from(item.bitmap.pixels) : undefined,
            },
        })), layers);

    addPayloadLayers(envelope, SemanticSegmentationMasksT, 'SemanticSegmentationMasks', (payload) =>
        payload.masks.map((item) => ({
            type: 'SemanticSegmentationMask',
            data: {
                ...objectData(item?.object),
                width: Number(item?.bitmap?.width || 0),
                height: Number(item?.bitmap?.height || 0),
                valueType: text(item?.bitmap?.valueType),
                pixels: item?.bitmap ? Uint8Array.from(item.bitmap.pixels) : undefined,
            },
        })), layers);

    addPayloadLayers(envelope, ObjectEmbeddingsT, 'ObjectEmbeddings', (payload) =>
        payload.embeddings.map((item) => ({
            type: 'ObjectEmbedding',
            data: {...objectData(item.object), values: [...item.values]},
        })), layers);

    for (const payload of envelope.for_each(PerformanceOverlayT)) {
        perfdata.push(...payload.lines.map(text));
        layers.push(layer(payload, 'PerformanceOverlay', payload.lines.map((line) => ({
            type: 'PerformanceLine', data: {text: text(line)},
        }))));
    }

    return {
        frame_counter: Number.isInteger(message.frame_counter) ? message.frame_counter : undefined,
        frame_results: {layers, perfdata},
    };
}
