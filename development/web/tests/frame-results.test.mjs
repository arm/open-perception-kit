import assert from 'node:assert/strict';
import test from 'node:test';

import {Builder, ByteBuffer} from 'flatbuffers';
import {Envelope, OPEN_PERCEPTION_KIT_NAME, OPEN_PERCEPTION_KIT_VERSION, SCHEMA_SET_SHA256} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/index.js';
import {WireEnvelope, WireEnvelopeT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/internalfb/wire-envelope.js';
import {WirePayload, WirePayloadT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/internalfb/wire-payload.js';
import {BoundingBoxT, LayerInfoT, ObjectMetaT, Point2fT, ProducerInfoT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata.js';
import {BoxDetectionT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/box-detection.js';
import {BoxDetectionsT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/box-detections.js';
import {ClassificationCandidateT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/classification-candidate.js';
import {ClassificationT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/classification.js';
import {ClassificationsT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/classifications.js';
import {FrameContextT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/frame-context.js';
import {ObjectTrackT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/object-track.js';
import {ObjectTracksT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/object-tracks.js';
import {PerformanceOverlay, PerformanceOverlayT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/performance-overlay.js';
import {PoseEstimationT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/pose-estimation.js';
import {PoseEstimationsT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/pose-estimations.js';
import {TrackTraceT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/track-trace.js';
import {TrackTracesT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/track-traces.js';
import {VideoFrameContextT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/video-frame-context.js';
import {_CLASS_TO_ID} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/registry.js';
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

function replacePayloadCount(encoded, count) {
    const packet = Buffer.from(encoded, 'base64');
    const view = new DataView(packet.buffer, packet.byteOffset, packet.byteLength);
    const root = view.getUint32(0, true);
    const vtable = root - view.getInt32(root, true);
    const payloadsOffset = view.getUint16(vtable + 4, true);
    assert.notEqual(payloadsOffset, 0);
    const payloadsField = root + payloadsOffset;
    const payloadsVector = payloadsField + view.getUint32(payloadsField, true);
    view.setInt32(payloadsVector, count, true);
    return packet.toString('base64');
}

function replaceNestedVectorCount(encoded, identifier, vtableField) {
    const packet = Buffer.from(encoded, 'base64');
    const envelope = WireEnvelope.getRootAsWireEnvelope(new ByteBuffer(packet));
    for (let index = 0; index < envelope.payloadsLength(); index += 1) {
        const payload = envelope.payloads(index, new WirePayload());
        const blob = payload?.blobArray();
        if (!blob || String.fromCharCode(...blob.subarray(4, 8)) !== identifier) continue;

        const view = new DataView(blob.buffer, blob.byteOffset, blob.byteLength);
        const root = view.getUint32(0, true);
        const vtable = root - view.getInt32(root, true);
        const fieldOffset = view.getUint16(vtable + vtableField, true);
        assert.notEqual(fieldOffset, 0);
        const field = root + fieldOffset;
        const vector = field + view.getUint32(field, true);
        view.setInt32(vector, blob.byteLength + 1, true);
        return packet;
    }
    throw new Error(`missing ${identifier} payload fixture`);
}

function registeredPerformanceOverlayFixture(blob) {
    const payloadId = _CLASS_TO_ID.get(PerformanceOverlayT);
    assert.notEqual(payloadId, undefined);
    const envelopeBuilder = new Builder(512);
    const envelope = new WireEnvelopeT(
        [new WirePayloadT(payloadId, [...blob])],
        OPEN_PERCEPTION_KIT_NAME,
        OPEN_PERCEPTION_KIT_VERSION,
        SCHEMA_SET_SHA256,
    ).pack(envelopeBuilder);
    WireEnvelope.finishWireEnvelopeBuffer(envelopeBuilder, envelope);
    return envelopeBuilder.asUint8Array();
}

function aliasedPerformanceOverlayFixture() {
    const payloadBuilder = new Builder(256);
    const shared = payloadBuilder.createString('blue'.repeat(16));
    const lines = PerformanceOverlay.createLinesVector(
        payloadBuilder,
        Array(16).fill(shared),
    );
    PerformanceOverlay.startPerformanceOverlay(payloadBuilder);
    PerformanceOverlay.addLines(payloadBuilder, lines);
    const payload = PerformanceOverlay.endPerformanceOverlay(payloadBuilder);
    PerformanceOverlay.finishPerformanceOverlayBuffer(payloadBuilder, payload);
    const blob = payloadBuilder.asUint8Array();
    assert.ok(blob.byteLength < 512);
    return registeredPerformanceOverlayFixture(blob);
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

test('rejects impossible payload counts and recovers', () => {
    const valid = encodedFixture();
    const decode = (packet) => decodeFrameResultsMessage({
        frame_results_encoding: FRAME_RESULTS_ENCODING,
        frame_results_packet_b64: packet,
    });

    assert.doesNotThrow(() => decode(valid));
    for (const count of [0x7fffffff, -1]) {
        assert.throws(
            () => decode(replacePayloadCount(valid, count)),
            FrameResultsDecodeError,
        );
        assert.doesNotThrow(() => decode(valid));
    }
});

test('rejects out-of-range nested vectors and recovers', () => {
    const valid = Buffer.from(encodedFixture(), 'base64');
    for (const [payloadType, identifier, vtableField] of [
        [PerformanceOverlayT, 'PERF', 8],
        [BoxDetectionsT, 'BDET', 10],
    ]) {
        const baseline = new Envelope(valid);
        assert.equal(baseline.count(payloadType), 1);

        const malformed = new Envelope(
            replaceNestedVectorCount(valid.toString('base64'), identifier, vtableField),
        );
        assert.equal(malformed.valid(), true);
        assert.equal(malformed.count(payloadType), 0);
        assert.equal(malformed.get(payloadType), null);

        const recovered = new Envelope(valid);
        assert.equal(recovered.count(payloadType), 1);
    }
});

test('isolates short registered payload blobs and recovers', () => {
    const valid = Buffer.from(encodedFixture(), 'base64');
    assert.equal(new Envelope(valid).count(PerformanceOverlayT), 1);

    for (const length of [0, 4]) {
        const malformed = new Envelope(
            registeredPerformanceOverlayFixture(new Uint8Array(length)),
        );
        assert.equal(malformed.valid(), true);
        assert.equal(malformed.count(PerformanceOverlayT), 0);
        assert.equal(malformed.contains(PerformanceOverlayT), false);
        assert.equal(malformed.get(PerformanceOverlayT), null);
        assert.deepEqual([...malformed.for_each(PerformanceOverlayT)], []);
        assert.equal(new Envelope(valid).count(PerformanceOverlayT), 1);
    }
});

test('bounds aggregate decode work for aliased strings and recovers', () => {
    const valid = Buffer.from(encodedFixture(), 'base64');
    assert.equal(new Envelope(valid).count(PerformanceOverlayT), 1);

    const aliased = new Envelope(aliasedPerformanceOverlayFixture());
    assert.equal(aliased.valid(), true);
    assert.equal(aliased.count(PerformanceOverlayT), 0);
    assert.equal(aliased.get(PerformanceOverlayT), null);

    assert.equal(new Envelope(valid).count(PerformanceOverlayT), 1);
});
