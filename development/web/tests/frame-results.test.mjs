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
import {BitmapDataT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/bitmap-data.js';
import {BoundingBoxT, LayerInfoT, ObjectMetaT, Point2fT, ProducerInfoT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata.js';
import {BoxDetectionT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/box-detection.js';
import {BoxDetectionsT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/box-detections.js';
import {ClassificationCandidateT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/classification-candidate.js';
import {ClassificationT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/classification.js';
import {ClassificationsT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/classifications.js';
import {FrameContextT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/frame-context.js';
import {InstanceSegmentationMaskT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/instance-segmentation-mask.js';
import {InstanceSegmentationMasksT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/instance-segmentation-masks.js';
import {ObjectTrackT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/object-track.js';
import {ObjectTracksT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/object-tracks.js';
import {PerformanceOverlayT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/performance-overlay.js';
import {PoseEstimationT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/pose-estimation.js';
import {PoseEstimationsT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/pose-estimations.js';
import {SegmentationMaskT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/segmentation-mask.js';
import {SegmentationMasksT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/segmentation-masks.js';
import {SemanticSegmentationMaskT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/semantic-segmentation-mask.js';
import {SemanticSegmentationMasksT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/semantic-segmentation-masks.js';
import {TrackTraceT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/track-trace.js';
import {TrackTracesT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/track-traces.js';
import {VideoFrameContextT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/video-frame-context.js';
import {decodeFrameResultsMessage, FRAME_RESULTS_ENCODING, FrameResultsDecodeError} from '../src/frame-results.js';
import {findParentRect, findVideoFrame} from '../src/osd-renderer.js';

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

function segmentationFixture(contentType, masks) {
    const envelope = new Envelope();
    envelope.add(new FrameContextT(
        1,
        0,
        layer('frameContext'),
        new VideoFrameContextT(new ObjectMetaT(10n), 4n, 3n),
    ));
    const segmentationLayer = layer(contentType);
    if (contentType === 'instanceSegmentation') segmentationLayer.labelFamily = 'coco';
    if (contentType === 'semanticSegmentation') segmentationLayer.labelFamily = 'cityscapes';
    const payloadType = {
        segmentation: SegmentationMasksT,
        instanceSegmentation: InstanceSegmentationMasksT,
        semanticSegmentation: SemanticSegmentationMasksT,
    }[contentType];
    envelope.add(new payloadType(1, 0, segmentationLayer, masks));
    return {
        frame_results_encoding: FRAME_RESULTS_ENCODING,
        frame_results_packet_b64: Buffer.from(envelope.serialize()).toString('base64'),
    };
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
    assert.equal(findParentRect(decoded.frame_results, 'humanFace', '20').text, 'face');
});

test('suppresses detector boxes replaced by tracker output', () => {
    const decoded = decodeFrameResultsMessage({
        frame_results_encoding: FRAME_RESULTS_ENCODING,
        frame_results_packet_b64: trackedFixture(),
    });

    const rects = decoded.frame_results.layers
        .filter((item) => item.contentType === 'humanFace')
        .flatMap((item) => item.detections)
        .filter((item) => item.type === 'Rect')
        .map((item) => item.data);

    assert.deepEqual(rects.map((item) => item.uuid), ['21', '30']);
    assert.equal(rects[0].text, 'untracked');
    assert.equal(rects[1].sourceId, '20');
    assert.equal(rects[1].trackId, '7');
});

test('decodes overlapping instance masks with independent pixels, classes, and source boxes', () => {
    const decoded = decodeFrameResultsMessage(segmentationFixture('instanceSegmentation', [
        new InstanceSegmentationMaskT(
            new ObjectMetaT(60n, 10n, 123n),
            new BitmapDataT(4, 3, 'Uint8', [
                0, 1, 1, 0,
                0, 1, 1, 0,
                0, 0, 0, 0,
            ]),
            0,
            0.75,
            new BoundingBoxT(0.25, 0.5, 2.5, 2),
        ),
        new InstanceSegmentationMaskT(
            new ObjectMetaT(61n, 10n, 456n),
            new BitmapDataT(4, 3, 'Uint8', [
                0, 0, 0, 0,
                0, 1, 1, 1,
                0, 1, 1, 1,
            ]),
            79,
            0.625,
            new BoundingBoxT(1.25, 1, 2.25, 1.5),
        ),
    ]));
    const maskLayer = decoded.frame_results.layers.find(
        (item) => item.contentType === 'instanceSegmentation',
    );

    assert.equal(maskLayer.labelFamily, 'coco');
    assert.equal(maskLayer.count, 2);
    assert.deepEqual(maskLayer.detections.map((item) => item.type), [
        'InstanceSegmentationMask', 'InstanceSegmentationMask',
    ]);
    const [first, second] = maskLayer.detections.map((item) => item.data);
    assert.deepEqual(first, {
        uuid: '60',
        parentUuid: '10',
        creationTsNs: '123',
        width: 4,
        height: 3,
        valueType: 'Uint8',
        pixels: Uint8Array.from([0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0]),
        classId: 0,
        confidence: 0.75,
        box: {x: 0.25, y: 0.5, width: 2.5, height: 2},
    });
    assert.deepEqual(second, {
        uuid: '61',
        parentUuid: '10',
        creationTsNs: '456',
        width: 4,
        height: 3,
        valueType: 'Uint8',
        pixels: Uint8Array.from([0, 0, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1]),
        classId: 79,
        confidence: 0.625,
        box: {x: 1.25, y: 1, width: 2.25, height: 1.5},
    });
    assert.equal(first.pixels[5], 1);
    assert.equal(second.pixels[5], 1);
    assert.notEqual(first.pixels, second.pixels);
    assert.equal(findVideoFrame(decoded.frame_results).uuid, first.parentUuid);
    assert.equal(decoded.frame_results.layers.some(
        (item) => item.detections.some((detection) => detection.type === 'Rect'),
    ), false);
});

test('preserves an empty instance-segmentation layer', () => {
    const decoded = decodeFrameResultsMessage(segmentationFixture('instanceSegmentation', []));
    const maskLayer = decoded.frame_results.layers.find(
        (item) => item.contentType === 'instanceSegmentation',
    );

    assert.equal(maskLayer.count, 0);
    assert.equal(maskLayer.labelFamily, 'coco');
    assert.deepEqual(maskLayer.detections, []);
});

test('preserves legacy segmentation alpha bytes without instance fields', () => {
    const decoded = decodeFrameResultsMessage(segmentationFixture('segmentation', [
        new SegmentationMaskT(
            new ObjectMetaT(70n, 10n),
            new BitmapDataT(2, 2, 'Uint8', [0, 127, 255, 3]),
        ),
    ]));
    const maskLayer = decoded.frame_results.layers.find(
        (item) => item.contentType === 'segmentation',
    );

    assert.equal(maskLayer.count, 1);
    assert.deepEqual(maskLayer.detections[0], {
        type: 'SegmentationMask',
        data: {
            uuid: '70',
            parentUuid: '10',
            creationTsNs: '0',
            width: 2,
            height: 2,
            valueType: 'Uint8',
            pixels: Uint8Array.from([0, 127, 255, 3]),
        },
    });
});

test('decodes semantic class zero and eighteen without instance metadata', () => {
    const decoded = decodeFrameResultsMessage(segmentationFixture('semanticSegmentation', [
        new SemanticSegmentationMaskT(
            new ObjectMetaT(80n, 10n, 789n),
            new BitmapDataT(4, 3, 'Uint8', [0, 18, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]),
        ),
    ]));
    const maskLayer = decoded.frame_results.layers.find(
        (item) => item.contentType === 'semanticSegmentation',
    );

    assert.equal(maskLayer.labelFamily, 'cityscapes');
    assert.equal(maskLayer.count, 1);
    assert.deepEqual(maskLayer.detections[0], {
        type: 'SemanticSegmentationMask',
        data: {
            uuid: '80',
            parentUuid: '10',
            creationTsNs: '789',
            width: 4,
            height: 3,
            valueType: 'Uint8',
            pixels: Uint8Array.from([0, 18, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]),
        },
    });
});

test('keeps semantic, instance, and legacy mask payloads distinct in the same packet', () => {
    const envelope = new Envelope();
    const maskLayer = layer('segmentation');
    envelope.add(new SegmentationMasksT(1, 0, maskLayer, [
        new SegmentationMaskT(new ObjectMetaT(70n, 10n), new BitmapDataT(2, 1, 'Uint8', [0, 127])),
    ]));
    // Reuse identical layer metadata so dispatch must follow the generated root types.
    envelope.add(new SemanticSegmentationMasksT(1, 0, maskLayer, [
        new SemanticSegmentationMaskT(new ObjectMetaT(80n, 10n), new BitmapDataT(2, 1, 'Uint8', [0, 18])),
    ]));
    envelope.add(new InstanceSegmentationMasksT(1, 0, maskLayer, [
        new InstanceSegmentationMaskT(
            new ObjectMetaT(60n, 10n), new BitmapDataT(2, 1, 'Uint8', [0, 1]),
            3, 0.5, new BoundingBoxT(0, 0, 2, 1),
        ),
    ]));
    const decoded = decodeFrameResultsMessage({
        frame_results_encoding: FRAME_RESULTS_ENCODING,
        frame_results_packet_b64: Buffer.from(envelope.serialize()).toString('base64'),
    });
    const detections = decoded.frame_results.layers.flatMap((item) => item.detections);

    assert.deepEqual(detections.map((item) => item.type), [
        'SegmentationMask', 'SemanticSegmentationMask', 'InstanceSegmentationMask',
    ]);
    assert.deepEqual(detections.map((item) => [...item.data.pixels]), [[0, 127], [0, 18], [0, 1]]);
    assert.equal('classId' in detections[0].data, false);
    assert.equal('classId' in detections[1].data, false);
    assert.equal(detections[2].data.classId, 3);
    assert.equal(detections[2].data.confidence, 0.5);
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
