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

test('contact anchors and radii use the full box at viewport edges', () => {
    for (const inContact of [false, true]) {
        for (const [x, y, width, height, cx, cy, radius, noContactRadius] of [
            [-10, 10, 20, 20, 0, 20, 18, 28],
            [118, 10, 20, 20, 128, 20, 18, 28],
            [-60, 0, 100, 80, -10, 40, 26, 46],
        ]) {
            const face = faceFixture();
            Object.assign(face.detections[0].data, {x, y, width, height});
            const contact = {payloadType: 'CameraContacts', contentType: 'cameraContact',
                detections: [{type: 'CameraContact', data: {subjectUuid: 'face', result: {inContact}}}]};
            const calls = render({layers: [face, contact]}, [select('camera-contact', contact)], 128, 96).calls;
            assert.deepEqual(calls.find(call => call.arc).arc.slice(0, 3), [cx, cy, inContact ? radius : noContactRadius]);
        }
    }
});

test('camera contact uses explicit outcome and subject independently of model index and derivation', () => {
    const face = faceFixture();
    face.contentType = 'custom-subject';
    const contact = {payloadType: 'CameraContacts', contentType: 'cameraContact',
        detections: [{type: 'CameraContact', data: {parentUuid: 'unrelated', subjectUuid: 'face',
            result: {inContact: true, modelClassIndex: null, confidence: .9}}}]};
    const overlay = select('camera-contact', contact);
    let calls = render({layers: [face, contact]}, [overlay]).calls;
    assert.equal(calls.filter((call) => call.arc).length, 2);
    assert.deepEqual(calls.find((call) => call.dash).dash, []);
    contact.detections[0].data.result = {inContact: true, modelClassIndex: 0, text: 'contact', confidence: 0.9};
    calls = render({layers: [face, contact]}, [overlay]).calls;
    assert.deepEqual(calls.find((call) => call.dash).dash, []);
    contact.detections[0].data.result.inContact = false;
    calls = render({layers: [face, contact]}, [overlay]).calls;
    assert.deepEqual(calls.find((call) => call.dash).dash, [6, 4]);
    assert.equal(resolveOverlay(indexFrame({layers: [contact]}), overlay).state, 'partial');
    const ambiguous = {layers: [face, {...face, model: 'another'}, contact]};
    assert.equal(resolveOverlay(indexFrame(ambiguous), overlay).state, 'ambiguous');
    assert.equal(render(ambiguous, [overlay]).calls.length, 0);
    const track = {...face, payloadType: 'ObjectTracks'};
    assert.equal(resolveOverlay(indexFrame({layers: [track, contact]}), overlay).state, 'ready');
    const invalid = {...track, detections: track.detections.map(item => ({...item, data: {...item.data, spatialValid: false}}))};
    assert.equal(resolveOverlay(indexFrame({layers: [face, invalid, contact]}), overlay).state, 'ambiguous');
    contact.detections[0].data.subjectUuid = null;
    contact.detections[0].data.parentUuid = 'face';
    assert.equal(render({layers: [face, contact]}, [overlay]).calls.length, 0);
    contact.detections[0].data.subjectUuid = 'face';
    for (const result of [null, {inContact: true, confidence: NaN}, {modelClassIndex: 99, confidence: .9}]) {
        contact.detections[0].data.result = result;
        assert.equal(render({layers: [face, contact]}, [overlay]).calls.length, 0);
    }
});
