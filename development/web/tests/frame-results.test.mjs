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

import {Builder, ByteBuffer} from 'flatbuffers';
import {Envelope, OPEN_PERCEPTION_KIT_NAME, OPEN_PERCEPTION_KIT_VERSION} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/index.js';
import {WireEnvelope, WireEnvelopeT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/internalfb/wire-envelope.js';
import {BitmapDataT, BoundingBoxT, LayerInfoT, ObjectMetaT, Point2fT, ProducerInfoT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata.js';
import {BoxDetectionT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/box-detection.js';
import {BoxDetectionsT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/box-detections.js';
import {ClassificationCandidateT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/classification-candidate.js';
import {ClassificationT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/classification.js';
import {ClassificationKind} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/classification-kind.js';
import {ClassificationsT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/classifications.js';
import {CameraContactsT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/camera-contacts.js';
import {CameraContactT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/camera-contact.js';
import {CameraContactResultT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/camera-contact-result.js';
import {PersonPresencesT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/person-presences.js';
import {PersonPresenceT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/person-presence.js';
import {PersonPresenceResultT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/person-presence-result.js';
import {FrameContextT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/frame-context.js';
import {ObjectTrackT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/object-track.js';
import {ObjectTracksT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/object-tracks.js';
import {PerformanceMeasurementsT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/performance-measurements.js';
import {FrameRateT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/frame-rate.js';
import {ScopeTimingT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/scope-timing.js';
import {ImageSpaceT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/image-space.js';
import {CoordinateUnits} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/coordinate-units.js';
import {GazeAnglesT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/gaze-angles.js';
import {GazeMeasurementT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/gaze-measurement.js';
import {GazeMeasurementsT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/gaze-measurements.js';
import {SegmentationMaskT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/segmentation-mask.js';
import {SegmentationMasksT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/segmentation-masks.js';
import {TrackTraceT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/track-trace.js';
import {TrackTracesT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/track-traces.js';
import {VideoFrameContextT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/video-frame-context.js';
import {decodeFrameResultsMessage, FRAME_RESULTS_ENCODING, FrameResultsDecodeError} from '../src/frame-results.js';
import {findVideoFrame} from '../src/overlay-drawing.js';
import {createCoordinateMapper} from '../src/overlay-drawing.js';
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
            new BoundingBoxT(10, 20, 30, 40, new ImageSpaceT(10n, 640, 480, 0, 0, 640, 480)),
            0.9,
            1,
            'face',
        )],
    ));
    envelope.add(new CameraContactsT(
        1,
        0,
        layer('cameraContact'),
        [new CameraContactT(
            new ObjectMetaT(30n, 20n),
            new CameraContactResultT(true, 0.8, 1), 20n,
        )],
    ));
    envelope.add(new GazeMeasurementsT(
        1,
        0,
        layer('eyeYawPitch'),
        [new GazeMeasurementT(new ObjectMetaT(40n, 999n), 20n, new GazeAnglesT(5, -2), 0.7)],
    ));
    envelope.add(new TrackTracesT(
        1,
        0,
        layer('trackTrace'),
        [new TrackTraceT(new ObjectMetaT(50n), 7n, [new Point2fT(1, 2), new Point2fT(3, 4)],
            new ImageSpaceT(10n, 640, 480, 0, 0, 640, 480))],
    ));
    envelope.add(new PerformanceMeasurementsT(1, 0, null, [], new FrameRateT(30)));
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
                new BoundingBoxT(10, 20, 30, 40, new ImageSpaceT(10n, 640, 480, 0, 0, 640, 480)),
                0.9,
                1,
                'tracked source',
            ),
            new BoxDetectionT(
                new ObjectMetaT(21n),
                new BoundingBoxT(50, 60, 20, 20, new ImageSpaceT(10n, 640, 480, 0, 0, 640, 480)),
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
            new BoundingBoxT(12, 22, 30, 40, new ImageSpaceT(10n, 640, 480, 0, 0, 640, 480)),
            0.95,
            1,
            'tracked',
        )],
    ));
    return Buffer.from(envelope.serialize()).toString('base64');
}

test('classification identity, candidate order and recognized text round trip without reinterpretation', () => {
    const envelope = new Envelope();
    const recognized = 'Hello\n世界';
    envelope.add(new ClassificationsT(1, 0, layer('classification'), [
        new ClassificationT(new ObjectMetaT(7n), [
            new ClassificationCandidateT(.8, 7, 'display label'),
            new ClassificationCandidateT(.2, 0, 'zero label')], 'test:labels', ClassificationKind.Categorical, 9007199254740993n),
        new ClassificationT(new ObjectMetaT(8n), [new ClassificationCandidateT(null, null, recognized)], '', ClassificationKind.RecognizedText),
        new ClassificationT(new ObjectMetaT(9n), []),
    ]));
    envelope.add(new CameraContactsT(1, 0, layer('cameraContact'), [
        new CameraContactT(new ObjectMetaT(10n), new CameraContactResultT(true, .5, 0), 7n),
        new CameraContactT(new ObjectMetaT(11n)),
    ]));
    envelope.add(new PersonPresencesT(1, 0, layer('personClassification'), [new PersonPresenceT(new ObjectMetaT(12n), new PersonPresenceResultT(true, 2, -1), 7n)]));
    const frame = decodeFrameResultsMessage({frame_results_encoding: FRAME_RESULTS_ENCODING,
        frame_results_packet_b64: Buffer.from(envelope.serialize()).toString('base64')}).frame_results;
    const classifications = frame.layers.find(item => item.payloadType === 'Classifications').detections;
    assert.equal(classifications[0].data.labelSet, 'test:labels');
    assert.equal(classifications[0].data.kind, ClassificationKind.Categorical);
    assert.equal(classifications[0].data.subjectUuid, '9007199254740993');
    assert.deepEqual(classifications[0].data.candidates.map(item => item.classId), [7, 0]);
    assert.equal(classifications[1].data.labelSet, '');
    assert.equal(classifications[1].data.candidates[0].text, recognized);
    assert.equal(classifications[1].data.candidates[0].classId, null);
    assert.equal(classifications[1].data.candidates[0].confidence, null);
    assert.equal(classifications[1].data.kind, ClassificationKind.RecognizedText);
    assert.deepEqual(classifications[2].data.candidates, []);
    const contacts = frame.layers.find(item => item.payloadType === 'CameraContacts').detections;
    assert.equal(contacts[0].data.subjectUuid, '7');
    assert.deepEqual(contacts[0].data.result, {inContact: true, modelClassIndex: 0, confidence: .5});
    assert.equal(contacts[1].data.result, null);
    assert.equal(frame.layers.find(item => item.payloadType === 'PersonPresences').detections[0].type, 'PersonClassification');
});

test('splitting classification roots preserves their mixed publication order', () => {
    const envelope = new Envelope();
    envelope.add(new PersonPresencesT(1, 0, layer('personClassification'), []));
    envelope.add(new CameraContactsT(1, 0, layer('cameraContact'), []));
    envelope.add(new ClassificationsT(1, 0, layer('classification'), []));
    envelope.add(new PersonPresencesT(1, 0, layer('personClassification'), []));
    const frame = decodeFrameResultsMessage({frame_results_encoding: FRAME_RESULTS_ENCODING,
        frame_results_packet_b64: Buffer.from(envelope.serialize()).toString('base64')}).frame_results;
    assert.deepEqual(frame.layers.map(item => item.payloadType), [
        'PersonPresences', 'CameraContacts', 'Classifications', 'PersonPresences']);
    const wire = WireEnvelope.getRootAsWireEnvelope(new ByteBuffer(envelope.serialize())).unpack();
    wire.payloads[0].blob = [0, 0, 0, 0];
    const builder = new Builder(1024);
    WireEnvelope.finishWireEnvelopeBuffer(builder, wire.pack(builder));
    const filtered = decodeFrameResultsMessage({frame_results_encoding: FRAME_RESULTS_ENCODING,
        frame_results_packet_b64: Buffer.from(builder.asUint8Array()).toString('base64')}).frame_results;
    assert.deepEqual(filtered.layers.map(item => item.payloadType), ['CameraContacts', 'Classifications', 'PersonPresences']);
});

test('presence decoding distinguishes unavailable and zero scores without coercing NaN', () => {
    const envelope = new Envelope();
    envelope.add(new PersonPresencesT(1, 0, layer('personClassification'), [
        new PersonPresenceT(new ObjectMetaT(1n)),
        new PersonPresenceT(new ObjectMetaT(2n), new PersonPresenceResultT(false, 0, 1)),
        new PersonPresenceT(new ObjectMetaT(3n), new PersonPresenceResultT(true, .8, NaN)),
    ]));
    const frame = decodeFrameResultsMessage({frame_results_encoding: FRAME_RESULTS_ENCODING,
        frame_results_packet_b64: Buffer.from(envelope.serialize()).toString('base64')}).frame_results;
    const data = frame.layers[0].detections.map(item => item.data);
    assert.equal(data[0].result, null);
    assert.equal(data[1].result.personPresent, false);
    assert.equal(data[1].result.yesScore, 0);
    assert.equal(data[1].result.noScore, 1);
    assert.ok(Number.isNaN(data[2].result.noScore));
});

test('classification decoding preserves non-finite confidence for validation', () => {
    const envelope = new Envelope();
    envelope.add(new ClassificationsT(1, 0, null, [new ClassificationT(null,
        [new ClassificationCandidateT(NaN, 1, 'label')], 'test:labels', ClassificationKind.Categorical)]));
    const frame = decodeFrameResultsMessage({frame_results_encoding: FRAME_RESULTS_ENCODING,
        frame_results_packet_b64: Buffer.from(envelope.serialize()).toString('base64')}).frame_results;
    assert.ok(Number.isNaN(frame.layers[0].detections[0].data.candidates[0].confidence));
});

test('decodes typed FrameResults into the established WebUI view model', () => {
    const decoded = decodeFrameResultsMessage({
        frame_counter: 42,
        frame_results_encoding: FRAME_RESULTS_ENCODING,
        frame_results_packet_b64: encodedFixture(),
    });

    assert.equal(decoded.frame_counter, 42);
    assert.equal(decoded.frame_results.performance[0].frameRate.fps, 30);
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
        spatialValid: true,
        frameWidth: 640,
        frameHeight: 480,
        confidence: 0.8999999761581421,
        classId: 1,
        text: 'face',
    });
    const gaze = decoded.frame_results.layers.find((item) => item.contentType === 'eyeYawPitch');
    assert.equal(gaze.detections[0].data.angles.yawDegrees, 5);
    assert.equal(gaze.detections[0].data.subjectUuid, '20');
    assert.equal(gaze.detections[0].data.parentUuid, '999');
    assert.equal(findVideoFrame(decoded.frame_results).originalHeight, 480);
    assert.equal(indexFrame(decoded.frame_results).objects.get('20')[0].data.text, 'face');
});

test('gaze decoding preserves optional values, precise subject IDs and non-finite angles', () => {
    const envelope = new Envelope();
    envelope.add(new GazeMeasurementsT(1, 0, layer('custom-gaze'), [
        new GazeMeasurementT(new ObjectMetaT(1n), null, null, null),
        new GazeMeasurementT(new ObjectMetaT(2n), 9007199254740993n, new GazeAnglesT(0, 0), 0),
        new GazeMeasurementT(new ObjectMetaT(3n), 20n, new GazeAnglesT(NaN, 15), .9),
    ]));
    const frame = decodeFrameResultsMessage({frame_results_encoding: FRAME_RESULTS_ENCODING,
        frame_results_packet_b64: Buffer.from(envelope.serialize()).toString('base64')}).frame_results;
    const data = frame.layers.find(item => item.payloadType === 'GazeMeasurements').detections.map(item => item.data);
    assert.equal(data[0].subjectUuid, null);
    assert.equal(data[0].angles, null);
    assert.equal(data[0].confidence, null);
    assert.equal(data[1].subjectUuid, '9007199254740993');
    assert.deepEqual(data[1].angles, {yawDegrees: 0, pitchDegrees: 0});
    assert.equal(data[1].confidence, 0);
    assert.ok(Number.isNaN(data[2].angles.yawDegrees));
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

test('projects cropped normalized geometry independently of parents and first frame metadata', () => {
    const envelope = new Envelope();
    // An unrelated first frame must not determine the box's coordinate basis.
    envelope.add(new FrameContextT(1, 0, layer('frameContext'),
        new VideoFrameContextT(new ObjectMetaT(99n), 10n, 10n)));
    const space = new ImageSpaceT(42n, 640, 480, 100, 50, 200, 100);
    envelope.add(new BoxDetectionsT(1, 0, layer('genericObject'), [
        new BoxDetectionT(new ObjectMetaT(7n, 999n), new BoundingBoxT(.25, .5, .5, .25, space, CoordinateUnits.Normalized)),
        new BoxDetectionT(new ObjectMetaT(8n, 99n), new BoundingBoxT(50, 50, 100, 25)),
    ]));
    const frame = decodeFrameResultsMessage({frame_results_encoding: FRAME_RESULTS_ENCODING,
        frame_results_packet_b64: Buffer.from(envelope.serialize()).toString('base64')}).frame_results;
    const [good, missing] = frame.layers.find(item => item.payloadType === 'BoxDetections').detections.map(item => item.data);
    assert.equal(good.parentUuid, '999');
    const mapper = createCoordinateMapper({canvasWidth: 1280, canvasHeight: 960,
        videoWidth: 1280, videoHeight: 960, perception: frame});
    assert.deepEqual(mapper.rect(good), {x: 300, y: 200, width: 200, height: 50});
    assert.equal(mapper.rect(missing), null);
});

test('retains empty outputs, mask bytes and performance payloads', () => {
    const envelope = new Envelope();
    envelope.add(new BoxDetectionsT(1, 0, layer('genericObject'), []));
    const maskLayer = layer('segmentation');
    maskLayer.compositingMode = 'backgroundReplacement';
    envelope.add(new SegmentationMasksT(1, 0, maskLayer, [
        new SegmentationMaskT(new ObjectMetaT(1n), new BitmapDataT(2, 2, 'Uint8', [0, 64, 128, 255])),
    ]));
    envelope.add(new PerformanceMeasurementsT(1, 0, 'BGRA', [new ScopeTimingT('Inference', 0, 2.5, 'infer')],
        new FrameRateT(null), new ProducerInfoT('metrics', 'opkperformance', '')));
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
    const performance = frame.layers.find((item) => item.payloadType === 'PerformanceMeasurements');
    assert.deepEqual(performance.detections[0].data, {
        pixelFormat: 'BGRA', producer: {instanceId: 'metrics', component: 'opkperformance', implementation: ''},
        timings: [{scopeId: 'infer', name: 'Inference', meanMs: 0, p95Ms: 2.5}], frameRate: {fps: null},
    });
    assert.deepEqual(frame.performance[0], performance.detections[0].data);
});

test('box and track category IDs preserve absence and zero, and confidence is not coerced', () => {
    const envelope = new Envelope();
    envelope.add(new BoxDetectionsT(1, 0, layer('genericObject'), [
        new BoxDetectionT(new ObjectMetaT(1n), null, NaN, null, 'unclassified'),
        new BoxDetectionT(new ObjectMetaT(2n), null, 0, 0, 'zero'),
    ]));
    const track = new ObjectTrackT();
    track.confidence = NaN;
    envelope.add(new ObjectTracksT(1, 0, layer('genericObject'), [track]));
    const frame = decodeFrameResultsMessage({frame_results_encoding: FRAME_RESULTS_ENCODING,
        frame_results_packet_b64: Buffer.from(envelope.serialize()).toString('base64')}).frame_results;
    const boxes = frame.layers.find(item => item.payloadType === 'BoxDetections').detections;
    assert.equal(boxes[0].data.classId, null);
    assert.ok(Number.isNaN(boxes[0].data.confidence));
    assert.equal(boxes[1].data.classId, 0);
    assert.equal(boxes[1].data.confidence, 0);
    const tracked = frame.layers.find(item => item.payloadType === 'ObjectTracks').detections[0].data;
    assert.equal(tracked.classId, null);
    assert.ok(Number.isNaN(tracked.confidence));
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
        /name_mismatch/,
    );
});

test('accepts same-major identities with different schema digests and rejects legacy versions', () => {
    const major = OPEN_PERCEPTION_KIT_VERSION.split('.')[0];
    for (const version of [OPEN_PERCEPTION_KIT_VERSION, `${major}.999.0`, '0.1.2', '9.9.9', 'invalid', null]) {
        const builder = new Builder(128);
        const offset = new WireEnvelopeT([], OPEN_PERCEPTION_KIT_NAME, version, 'different-digest').pack(builder);
        WireEnvelope.finishWireEnvelopeBuffer(builder, offset);
        const decode = () => decodeFrameResultsMessage({
            frame_results_encoding: FRAME_RESULTS_ENCODING,
            frame_results_packet_b64: Buffer.from(builder.asUint8Array()).toString('base64'),
        });
        if (version === OPEN_PERCEPTION_KIT_VERSION || version === `${major}.999.0`) {
            assert.deepEqual(decode().frame_results.layers, []);
        } else {
            assert.throws(decode, /version_mismatch/);
        }
    }
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
        assert.deepEqual(events.at(-1).detail.performance, {measurements: [
            {pixelFormat: '', producer: null, timings: [], frameRate: {fps: 30}},
        ]});
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
