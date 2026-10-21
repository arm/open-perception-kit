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

test('gaze joins parent faces without drawing them, and rejects missing/ambiguous parents', () => {
    const face = faceFixture();
    const gaze = {payloadType: 'PoseEstimations', contentType: 'eyeYawPitch',
        detections: [{type: 'YawPitch', data: {parentUuid: 'face', yaw: 10, pitch: 2}}]};
    const overlay = select('gaze', gaze);
    const calls = render({layers: [face, gaze]}, [overlay]).calls;
    assert.equal(calls.filter((call) => call.stroke).length, 1);
    assert.equal(calls.filter((call) => call.arc).length, 0);
    assert.equal(resolveOverlay(indexFrame({layers: [gaze]}), overlay).state, 'partial');
    const duplicate = {layers: [face, {...face, model: 'other'}, gaze]};
    assert.equal(resolveOverlay(indexFrame(duplicate), overlay).state, 'ambiguous');
    assert.equal(render(duplicate, [overlay]).calls.length, 0);
    const unsupported = {...gaze, contentType: 'bodyPose'};
    assert.equal(resolveOverlay(indexFrame({layers: [unsupported]}), select('gaze', unsupported)).state, 'unsupported');
});

test('gaze projection does not reverse across ninety degrees', () => {
    const start = {x: 160, y: 120};
    const before = gazeEndpoint(start, 88, 0, 120);
    const after = gazeEndpoint(start, 92, 0, 120);
    assert.ok(before.x < start.x && after.x < start.x);
    assert.ok(Math.abs(before.x - after.x) < 1e-9);
});
