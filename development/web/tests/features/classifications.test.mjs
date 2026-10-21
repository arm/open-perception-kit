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
import {classificationHeading, classificationPanelHeight, classificationTextX} from '../../src/overlay-drawing.js';
import {render, select} from '../overlay-test-utils.mjs';

test('classification labels retain producer headings and separate left/right columns', () => {
    assert.equal(classificationHeading({producer: {implementation: 'Parser'}}), 'Parser');
    assert.equal(classificationHeading({}), '');
    assert.equal(classificationPanelHeight(5, 'Parser'), 146);
    assert.equal(classificationPanelHeight(5, ''), 125);
    assert.equal(classificationTextX({x: 20, width: 1000}, 10, true), 1010);
    const first = {payloadType: 'Classifications', contentType: 'classification', compositingMode: 'bottomLeft',
        producer: {implementation: 'Parser'}, detections: [{type: 'Classification', data: {candidates: [{text: 'chainlink fence', classId: 489, confidence: 0.56}]}}]};
    const second = {...first, compositingMode: 'bottomRight', producer: {implementation: 'script.py'}};
    for (const width of [640, 1000]) {
        const calls = render({layers: [first, second]}, [select('classifications', first), select('classifications', second)], width, 360).calls;
        assert.deepEqual(calls.map((call) => call.text), ['Parser', '#1: chainlink fence (56.0%)', 'script.py', '#1: chainlink fence (56.0%)']);
        assert.ok(calls[0].x < 20);
        assert.ok(calls[2].x > 400);
    }
    const empty = {...first, detections: [{type: 'Classification', data: {candidates: []}}]};
    assert.equal(render({layers: [empty]}, [select('classifications', empty)]).calls.length, 0);
});

test('person presence uses the classification feature without implying face contact', () => {
    const layer = {payloadType: 'Classifications', contentType: 'personClassification',
        detections: [{type: 'PersonClassification', data: {yesConfidence: 0.9, noConfidence: 0.1}}]};
    assert.equal(render({layers: [layer]}, [select('classifications', layer)]).calls[0].text, 'PERSON');
    layer.detections[0].data.noConfidence = 1;
    assert.equal(render({layers: [layer]}, [select('classifications', layer)]).calls[0].text, 'NON-PERSON');
});
