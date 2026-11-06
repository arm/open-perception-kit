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
import {gazeEndpoint} from '../../src/overlay-drawing.js';
import {resolveOverlay} from '../../src/overlay-features.js';
import {indexFrame} from '../../src/overlay-sources.js';
import {render, select, faceFixture} from '../overlay-test-utils.mjs';

test('typed gaze uses the explicit subject independently of derivation and content strings', () => {
    const face = faceFixture();
    face.contentType = 'unlabelled-subject';
    const gaze = {payloadType: 'GazeMeasurements', contentType: 'custom-gaze',
        detections: [{type: 'GazeMeasurement', data: {parentUuid: 'unrelated', subjectUuid: 'face',
            angles: {yawDegrees: 10, pitchDegrees: 2}, confidence: null}}]};
    const overlay = select('gaze', gaze);
    const calls = render({layers: [face, gaze]}, [overlay]).calls;
    assert.equal(calls.filter((call) => call.stroke).length, 1);
    assert.equal(calls.filter((call) => call.arc).length, 0);
    assert.equal(resolveOverlay(indexFrame({layers: [gaze]}), overlay).state, 'partial');
    const duplicate = {layers: [face, {...face, model: 'other'}, gaze]};
    assert.equal(resolveOverlay(indexFrame(duplicate), overlay).state, 'ambiguous');
    assert.equal(render(duplicate, [overlay]).calls.length, 0);
    const track = {...face, payloadType: 'ObjectTracks'};
    assert.equal(render({layers: [track, gaze]}, [overlay]).calls.filter(call => call.stroke).length, 1);
    const invalidDuplicate = {...track, detections: face.detections.map(item => ({...item, data: {...item.data, spatialValid: false}}))};
    assert.equal(resolveOverlay(indexFrame({layers: [face, invalidDuplicate, gaze]}), overlay).state, 'ambiguous');
    assert.equal(render({layers: [face, invalidDuplicate, gaze]}, [overlay]).calls.length, 0);
    assert.equal(resolveOverlay(indexFrame({layers: [invalidDuplicate, gaze]}), overlay).state, 'partial');
    gaze.detections[0].data.parentUuid = 'face';
    for (const subjectUuid of [null, '0', 'missing']) {
        gaze.detections[0].data.subjectUuid = subjectUuid;
        assert.equal(render({layers: [face, gaze]}, [overlay]).calls.length, 0);
        assert.equal(resolveOverlay(indexFrame({layers: [face, gaze]}), overlay).state, 'partial');
    }
    const unsupported = {...gaze, payloadType: 'UnrelatedPayload'};
    assert.equal(resolveOverlay(indexFrame({layers: [unsupported]}), select('gaze', unsupported)).state, 'unsupported');
});

test('zero angles are measurements while absent and non-finite pairs are unavailable', () => {
    const face = faceFixture();
    const gaze = {payloadType: 'GazeMeasurements', detections: [{type: 'GazeMeasurement',
        data: {subjectUuid: 'face', angles: {yawDegrees: 0, pitchDegrees: 0}, confidence: 0}}]};
    const overlay = select('gaze', gaze);
    assert.equal(resolveOverlay(indexFrame({layers: [face, gaze]}), overlay).state, 'ready');
    assert.equal(render({layers: [face, gaze]}, [overlay]).calls.length, 0);
    for (const angles of [null, {yawDegrees: NaN, pitchDegrees: 15}, {yawDegrees: 15, pitchDegrees: Infinity}]) {
        gaze.detections[0].data.angles = angles;
        assert.equal(resolveOverlay(indexFrame({layers: [face, gaze]}), overlay).state, 'partial');
        assert.equal(render({layers: [face, gaze]}, [overlay]).calls.length, 0);
    }
    gaze.detections[0].data.angles = {yawDegrees: 0, pitchDegrees: 15};
    gaze.detections[0].data.confidence = NaN;
    assert.equal(render({layers: [face, gaze]}, [overlay]).calls.length, 0);
});

test('degree projection preserves directions, zero and periodic equivalents', () => {
    const start = {x: 160, y: 120};
    assert.ok(gazeEndpoint(start, 30, 0, 120).x < start.x);
    assert.ok(gazeEndpoint(start, -30, 0, 120).x > start.x);
    assert.ok(gazeEndpoint(start, 0, 30, 120).y < start.y);
    assert.ok(gazeEndpoint(start, 0, -30, 120).y > start.y);
    assert.deepEqual(gazeEndpoint(start, 0, 0, 120), start);
    assert.deepEqual(gazeEndpoint(start, 390, -360, 120), gazeEndpoint(start, 30, 0, 120));
    assert.ok(Number.isNaN(gazeEndpoint(start, NaN, 15, 120).x));
});

test('gaze projection does not reverse across ninety degrees', () => {
    const start = {x: 160, y: 120};
    const before = gazeEndpoint(start, 88, 0, 120);
    const after = gazeEndpoint(start, 92, 0, 120);
    assert.ok(before.x < start.x && after.x < start.x);
    assert.ok(Math.abs(before.x - after.x) < 1e-9);
});
