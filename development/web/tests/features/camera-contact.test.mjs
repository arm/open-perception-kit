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
import {resolveOverlay} from '../../src/overlay-features.js';
import {indexFrame} from '../../src/overlay-sources.js';
import {render, select, faceFixture} from '../overlay-test-utils.mjs';

test('camera contact resolves hidden parent geometry and distinguishes contact with ring style', () => {
    const face = faceFixture();
    const contact = {payloadType: 'Classifications', contentType: 'cameraContact',
        detections: [{type: 'Classification', data: {parentUuid: 'face', candidates: [{classId: 1}]}}]};
    const overlay = select('camera-contact', contact);
    let calls = render({layers: [face, contact]}, [overlay]).calls;
    assert.equal(calls.filter((call) => call.arc).length, 2);
    assert.deepEqual(calls.find((call) => call.dash).dash, []);
    contact.detections[0].data.candidates[0].classId = 0;
    calls = render({layers: [face, contact]}, [overlay]).calls;
    assert.deepEqual(calls.find((call) => call.dash).dash, [6, 4]);
    assert.equal(resolveOverlay(indexFrame({layers: [contact]}), overlay).state, 'partial');
    const ambiguous = {layers: [face, {...face, model: 'another'}, contact]};
    assert.equal(resolveOverlay(indexFrame(ambiguous), overlay).state, 'ambiguous');
    assert.equal(render(ambiguous, [overlay]).calls.length, 0);
});
