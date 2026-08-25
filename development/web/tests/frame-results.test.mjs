import assert from 'node:assert/strict';
import test from 'node:test';

import {Builder} from 'flatbuffers';
import {Envelope} from '../../../generated/perception/ts/dist/perception/index.js';
import {WireEnvelope, WireEnvelopeT} from '../../../generated/perception/ts/dist/perception/fb/perception/internalfb/wire-envelope.js';
import {BoundingBoxT, LayerInfoT, ObjectMetaT, Point2fT} from '../../../generated/perception/ts/dist/perception/fb/perception/metadata.js';
import {BoxDetectionT} from '../../../generated/perception/ts/dist/perception/fb/perception/metadata/box-detection.js';
import {BoxDetectionsT} from '../../../generated/perception/ts/dist/perception/fb/perception/metadata/box-detections.js';
import {ClassificationCandidateT} from '../../../generated/perception/ts/dist/perception/fb/perception/metadata/classification-candidate.js';
import {ClassificationT} from '../../../generated/perception/ts/dist/perception/fb/perception/metadata/classification.js';
import {ClassificationsT} from '../../../generated/perception/ts/dist/perception/fb/perception/metadata/classifications.js';
import {FrameContextT} from '../../../generated/perception/ts/dist/perception/fb/perception/metadata/frame-context.js';
import {ObjectTrackT} from '../../../generated/perception/ts/dist/perception/fb/perception/metadata/object-track.js';
import {ObjectTracksT} from '../../../generated/perception/ts/dist/perception/fb/perception/metadata/object-tracks.js';
import {PerformanceOverlayT} from '../../../generated/perception/ts/dist/perception/fb/perception/metadata/performance-overlay.js';
import {PoseEstimationT} from '../../../generated/perception/ts/dist/perception/fb/perception/metadata/pose-estimation.js';
import {PoseEstimationsT} from '../../../generated/perception/ts/dist/perception/fb/perception/metadata/pose-estimations.js';
import {TrackTraceT} from '../../../generated/perception/ts/dist/perception/fb/perception/metadata/track-trace.js';
import {TrackTracesT} from '../../../generated/perception/ts/dist/perception/fb/perception/metadata/track-traces.js';
import {VideoFrameContextT} from '../../../generated/perception/ts/dist/perception/fb/perception/metadata/video-frame-context.js';
import {decodeFrameResultsMessage, FRAME_RESULTS_ENCODING, FrameResultsDecodeError} from '../src/frame-results.js';
import {findParentRect, findVideoFrame} from '../src/osd-renderer.js';

function layer(contentType) {
    return new LayerInfoT('engine', 'model', '', 'infer', 'labels', contentType, '');
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
