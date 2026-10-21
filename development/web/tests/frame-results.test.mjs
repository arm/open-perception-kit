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

import assert from 'node:assert/strict';
import test from 'node:test';

import {Builder} from 'flatbuffers';
import {Envelope} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/index.js';
import {WireEnvelope, WireEnvelopeT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/internalfb/wire-envelope.js';
import {BitmapDataT, BoundingBoxT, LayerInfoT, ObjectMetaT, Point2fT, ProducerInfoT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata.js';
import {BoxDetectionT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/box-detection.js';
import {BoxDetectionsT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/box-detections.js';
import {ClassificationCandidateT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/classification-candidate.js';
import {ClassificationT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/classification.js';
import {ClassificationsT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/classifications.js';
import {FrameContextT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/frame-context.js';
import {ObjectTrackT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/object-track.js';
import {ObjectTracksT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/object-tracks.js';
import {PerformanceOverlayT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/performance-overlay.js';
import {PoseEstimationT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/pose-estimation.js';
import {PoseEstimationsT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/pose-estimations.js';
import {SegmentationMaskT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/segmentation-mask.js';
import {SegmentationMasksT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/segmentation-masks.js';
import {TrackTraceT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/track-trace.js';
import {TrackTracesT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/track-traces.js';
import {VideoFrameContextT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/video-frame-context.js';
import {decodeFrameResultsMessage, FRAME_RESULTS_ENCODING, FrameResultsDecodeError} from '../src/frame-results.js';
import {findVideoFrame} from '../src/overlay-drawing.js';
import {indexFrame, outputSource} from '../src/overlay-sources.js';
import {createOverlay} from '../src/overlays.js';
import {resolveOverlay} from '../src/overlay-features.js';

function layer(contentType) {
    return new LayerInfoT(
        'engine',
        'model',
        '',
        'infer',
        'labels',
        contentType,
        '',
        new ProducerInfoT('infer/parser', 'opk-std-ops/GenericPostprocess', 'FixtureParser'),
    );
}

function encodedFixture() {
    const envelope = new Envelope();
    envelope.add(new FrameContextT(
        1,
        0,
        layer('videoFrame'),
        new VideoFrameContextT(new ObjectMetaT(10n), 640n, 480n),
    ));
    envelope.add(new BoxDetectionsT(
        1,
        0,
        layer('humanFace'),
        [new BoxDetectionT(
            new ObjectMetaT(20n, 0n),
            new BoundingBoxT(10, 20, 30, 40),
            0.9,
            1,
            'face',
        )],
    ));
    envelope.add(new ClassificationsT(
        1,
        0,
        layer('cameraContact'),
        [new ClassificationT(
            new ObjectMetaT(30n, 20n),
            [new ClassificationCandidateT(0.8, 1, 'contact')],
        )],
    ));
    envelope.add(new PoseEstimationsT(
        1,
        0,
        layer('eyeYawPitch'),
        [new PoseEstimationT(new ObjectMetaT(40n, 20n), 0.7, 5, -2)],
    ));
    envelope.add(new TrackTracesT(
        1,
        0,
        layer('trackTrace'),
        [new TrackTraceT(new ObjectMetaT(50n), 7n, [new Point2fT(1, 2), new Point2fT(3, 4)])],
    ));
    envelope.add(new PerformanceOverlayT(1, 0, ['Pipeline: 30 FPS']));
    return Buffer.from(envelope.serialize()).toString('base64');
}

function trackedFixture() {
    const envelope = new Envelope();
    envelope.add(new BoxDetectionsT(
        1,
        0,
        layer('humanFace'),
        [
            new BoxDetectionT(
                new ObjectMetaT(20n),
                new BoundingBoxT(10, 20, 30, 40),
                0.9,
                1,
                'tracked source',
            ),
            new BoxDetectionT(
                new ObjectMetaT(21n),
                new BoundingBoxT(50, 60, 20, 20),
                0.8,
                1,
                'untracked',
            ),
        ],
    ));
    envelope.add(new ObjectTracksT(
        1,
        0,
        layer('humanFace'),
        [new ObjectTrackT(
            new ObjectMetaT(30n),
            20n,
            7n,
            new BoundingBoxT(12, 22, 30, 40),
            0.95,
            1,
            'tracked',
        )],
    ));
    return Buffer.from(envelope.serialize()).toString('base64');
}

test('decodes typed FrameResults into the established WebUI view model', () => {
    const decoded = decodeFrameResultsMessage({
        frame_counter: 42,
        frame_results_encoding: FRAME_RESULTS_ENCODING,
        frame_results_packet_b64: encodedFixture(),
    });

    assert.equal(decoded.frame_counter, 42);
    assert.equal(decoded.frame_results.perfdata[0], 'Pipeline: 30 FPS');
    const video = decoded.frame_results.layers.find((item) => item.contentType === 'videoFrame');
    assert.equal(video.detections[0].data.originalWidth, 640);
    const face = decoded.frame_results.layers.find((item) => item.contentType === 'humanFace');
    assert.equal(face.payloadType, 'BoxDetections');
    assert.ok(face.payload instanceof BoxDetectionsT);
    assert.deepEqual(face.producer, {
        instanceId: 'infer/parser',
        component: 'opk-std-ops/GenericPostprocess',
        implementation: 'FixtureParser',
    });
    assert.deepEqual(face.detections[0].data, {
        uuid: '20',
        parentUuid: '0',
        creationTsNs: '0',
        x: 10,
        y: 20,
        width: 30,
        height: 40,
        confidence: 0.8999999761581421,
        classId: 1,
        text: 'face',
    });
    const gaze = decoded.frame_results.layers.find((item) => item.contentType === 'eyeYawPitch');
    assert.equal(gaze.detections[0].data.parentUuid, '20');
    assert.equal(findVideoFrame(decoded.frame_results).originalHeight, 480);
    assert.equal(indexFrame(decoded.frame_results).objects.get('20')[0].data.text, 'face');
});

test('retains detector and tracker outputs for independent overlay selection', () => {
    const decoded = decodeFrameResultsMessage({
        frame_results_encoding: FRAME_RESULTS_ENCODING,
        frame_results_packet_b64: trackedFixture(),
    });

    const rects = decoded.frame_results.layers
        .filter((item) => item.contentType === 'humanFace')
        .flatMap((item) => item.detections)
        .filter((item) => item.type === 'Rect')
        .map((item) => item.data);

    assert.deepEqual(rects.map((item) => item.uuid), ['20', '21', '30']);
    assert.equal(rects[1].text, 'untracked');
    assert.equal(rects[2].sourceId, '20');
    assert.equal(rects[2].trackId, '7');
    assert.deepEqual(decoded.frame_results.layers.map((item) => item.payloadType), ['BoxDetections', 'ObjectTracks']);
});

test('retains empty outputs, mask bytes and performance payloads', () => {
    const envelope = new Envelope();
    envelope.add(new BoxDetectionsT(1, 0, layer('genericObject'), []));
    const maskLayer = layer('segmentation');
    maskLayer.compositingMode = 'backgroundReplacement';
    envelope.add(new SegmentationMasksT(1, 0, maskLayer, [
        new SegmentationMaskT(new ObjectMetaT(1n), new BitmapDataT(2, 2, 'Uint8', [0, 64, 128, 255])),
    ]));
    envelope.add(new PerformanceOverlayT(1, 0, ['Pipeline: 30 FPS']));
    const {frame_results: frame} = decodeFrameResultsMessage({
        frame_results_encoding: FRAME_RESULTS_ENCODING,
        frame_results_packet_b64: Buffer.from(envelope.serialize()).toString('base64'),
    });
    assert.equal(frame.layers[0].payloadType, 'BoxDetections');
    assert.equal(frame.layers[0].count, 0);
    const mask = frame.layers.find((item) => item.payloadType === 'SegmentationMasks');
    assert.deepEqual([...mask.detections[0].data.pixels], [0, 64, 128, 255]);
    assert.ok(mask.detections[0].data.pixels instanceof Uint8Array);
    assert.deepEqual([...mask.detections[0].data.pixels], mask.payload.masks[0].bitmap.pixels);
    assert.equal(resolveOverlay(indexFrame(frame), createOverlay('background-replacement', outputSource(mask))).state, 'ready');
    const performance = frame.layers.find((item) => item.payloadType === 'PerformanceOverlay');
    assert.equal(performance.detections[0].data.text, frame.perfdata[0]);
});

test('rejects a packet from an incompatible producer identity', () => {
    const builder = new Builder(128);
    const offset = new WireEnvelopeT(
        [],
        'other_sdk',
        '0.1.0',
        '0'.repeat(64),
    ).pack(builder);
    WireEnvelope.finishWireEnvelopeBuffer(builder, offset);

    assert.throws(
        () => decodeFrameResultsMessage({
            frame_results_encoding: FRAME_RESULTS_ENCODING,
            frame_results_packet_b64: Buffer.from(builder.asUint8Array()).toString('base64'),
        }),
        /sdk_name_mismatch/,
    );
});

test('rejects unsupported encoding and malformed base64', () => {
    assert.throws(
        () => decodeFrameResultsMessage({frame_results_encoding: 'json'}),
        FrameResultsDecodeError,
    );
    assert.throws(
        () => decodeFrameResultsMessage({
            frame_results_encoding: FRAME_RESULTS_ENCODING,
            frame_results_packet_b64: 'not-base64',
        }),
        FrameResultsDecodeError,
    );
});

test('metadata connection reports disconnects and invalid packets clear the current frame', async () => {
    const originals = Object.fromEntries(['window', 'location', 'WebSocket', 'CustomEvent']
        .map((key) => [key, Object.getOwnPropertyDescriptor(globalThis, key)]));
    const events = [];
    const listeners = new Map();
    let socket;
    globalThis.window = {OPK_CONFIG: {}, dispatchEvent: (event) => events.push(event),
        addEventListener: (name, callback) => listeners.set(name, callback), setTimeout() {}};
    globalThis.location = {protocol: 'http:', hostname: 'localhost'};
    globalThis.CustomEvent = class {
        constructor(type, options) { this.type = type; this.detail = options.detail; }
    };
    globalThis.WebSocket = class {
        constructor() { socket = this; }
    };
    try {
        await import('../src/metadata-ws.js');
        socket.onopen();
        assert.deepEqual(events.at(-1).detail, {connected: true});
        socket.onmessage({data: JSON.stringify({
            frame_results_encoding: FRAME_RESULTS_ENCODING,
            frame_results_packet_b64: encodedFixture(),
        })});
        assert.ok(events.at(-1).detail.frame_results.layers.length > 0);
        socket.onmessage({data: JSON.stringify({frame_results_encoding: 'unsupported'})});
        assert.equal(events.at(-1).detail.frame_results, null);
        assert.ok(events.at(-1).detail.decode_error);
        socket.onmessage({data: '{'});
        assert.equal(events.at(-1).detail.frame_results, null);
        assert.equal(events.at(-1).detail.decode_error, 'Invalid metadata JSON');
        socket.onclose();
        assert.equal(events.at(-1).type, 'metadata-connection');
        assert.deepEqual(events.at(-1).detail, {connected: false});
        listeners.get('video-frame-metadata')({detail: {
            frame_results_encoding: FRAME_RESULTS_ENCODING,
            frame_results_packet_b64: encodedFixture(),
        }});
        assert.equal(events.at(-1).type, 'video-frame-results');
        assert.ok(events.at(-1).detail.layers.length > 0);
        listeners.get('video-frame-metadata')({detail: null});
        assert.equal(events.at(-1).detail, null);
        listeners.get('video-frame-metadata')({detail: {frame_results_encoding: 'invalid'}});
        assert.equal(events.at(-1).detail, null);
    } finally {
        for (const [key, descriptor] of Object.entries(originals)) {
            if (descriptor) Object.defineProperty(globalThis, key, descriptor);
            else delete globalThis[key];
        }
    }
});
