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
import {featureFor, resolveOverlay} from '../../src/overlay-features.js';
import {indexFrame} from '../../src/overlay-sources.js';
import {render, select, trackerFixture} from '../overlay-test-utils.mjs';

test('tracked objects joins by producer/track ID, preserving partial and ambiguous results', () => {
    const {tracks, trails} = trackerFixture();
    const renamed = {...trails, model: 'different label', engine: 'different engine', labelFamily: 'another family'};
    const unrelated = {...trails, producer: {...trails.producer, instanceId: 'another-tracker'}};
    const otherTrack = {...trails, detections: [{...trails.detections[0], data: {...trails.detections[0].data, trackId: '8'}}]};
    const overlay = select('tracked-objects', tracks, {color: '#123456', opacity: 0.4});
    const frame = {layers: [tracks, renamed, unrelated, otherTrack]};
    assert.deepEqual(render(frame, [overlay]).calls, [
        {stroke: 'rgba(18, 52, 86, 1)', alpha: 0.4}, {rect: [20, 30, 40, 50], color: '#123456', alpha: 0.4},
    ]);
    assert.equal(resolveOverlay(indexFrame(frame), overlay).state, 'ready');
    assert.equal(resolveOverlay(indexFrame(null), overlay).state, 'waiting');
    const missing = resolveOverlay(indexFrame({layers: [tracks, unrelated]}), overlay);
    assert.equal(missing.state, 'partial');
    assert.equal(missing.trails.length, 0);
    const ambiguous = resolveOverlay(indexFrame({layers: [tracks, trails, renamed]}), overlay);
    assert.equal(ambiguous.state, 'ambiguous');
    assert.equal(ambiguous.trails.length, 0);
    const unidentified = {...tracks, producer: null};
    assert.equal(resolveOverlay(indexFrame({layers: [unidentified, {...trails, producer: null}]}), select('tracked-objects', unidentified)).state, 'partial');
    const zeroId = {...tracks, detections: [{type: 'Rect', data: {...tracks.detections[0].data, trackId: '0'}}]};
    assert.equal(resolveOverlay(indexFrame({layers: [zeroId, trails]}), overlay).trails.length, 0);
    assert.equal(resolveOverlay(indexFrame({layers: [{...tracks, detections: []}]}), overlay).state, 'ready');
});

test('tracked-object controls independently select boxes and trails and validate saved booleans', () => {
    const {tracks, trails} = trackerFixture();
    const frame = {layers: [tracks, trails]};
    const overlay = select('tracked-objects', tracks);
    assert.equal(overlay.settings.boxes, true);
    assert.equal(overlay.settings.trails, true);
    assert.equal(featureFor(overlay.featureId).validateSettings({...overlay.settings, trails: 'yes'}), null);
    overlay.settings.trails = false;
    assert.equal(render(frame, [overlay]).calls.filter((call) => call.stroke).length, 0);
    assert.equal(render(frame, [overlay]).calls.filter((call) => call.rect).length, 1);
    assert.equal(resolveOverlay(indexFrame({layers: [tracks]}), overlay).state, 'ready');
    overlay.settings.boxes = false;
    overlay.settings.trails = true;
    assert.equal(render(frame, [overlay]).calls.filter((call) => call.rect).length, 0);
    assert.equal(render(frame, [overlay]).calls.filter((call) => call.stroke).length, 1);
    overlay.enabled = false;
    assert.equal(render(frame, [overlay]).calls.length, 0);
});
