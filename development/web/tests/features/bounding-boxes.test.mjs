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
import {featureFor} from '../../src/overlay-features.js';
import {outputSource, sourceKey} from '../../src/overlay-sources.js';
import {render, select, trackerFixture} from '../overlay-test-utils.mjs';

test('bounding boxes accepts detector/tracker regions, excludes faces, and isolates sources', () => {
    const {tracks, trails} = trackerFixture();
    const detector = {...tracks, payloadType: 'BoxDetections'};
    const other = {...detector, producer: {...detector.producer, instanceId: 'other'}};
    const faces = {...detector, contentType: 'humanFace'};
    const catalog = new Map([detector, tracks, faces, trails].map((layer) => [sourceKey(outputSource(layer)), outputSource(layer)]));
    assert.deepEqual(featureFor('bounding-boxes').discover(catalog), [outputSource(detector), outputSource(tracks)]);
    assert.equal(featureFor('bounding-boxes').defaults(outputSource(detector)).color, '#ff3030');
    const selected = select('bounding-boxes', detector, {color: '#123456', opacity: 0.4});
    assert.deepEqual(render({layers: [detector, tracks, other, trails]}, [selected]).calls,
        [{rect: [20, 30, 40, 50], color: '#123456', alpha: 0.4}]);
    assert.equal(render({layers: [faces]}, [select('bounding-boxes', faces)]).calls.length, 0);
    assert.equal(render({layers: [tracks, trails]}, [select('bounding-boxes', tracks)]).calls.filter((call) => call.stroke).length, 0);
});
