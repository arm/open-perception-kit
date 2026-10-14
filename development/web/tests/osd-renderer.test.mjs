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
import {calculateContainedRect, createCoordinateMapper, findVideoFrame} from '../src/overlay-drawing.js';
import {defineFeature} from '../src/overlay-feature.js';
import {OVERLAY_FEATURES} from '../src/overlay-features.js';
import {render, select} from './overlay-test-utils.mjs';

test('source geometry is mapped into the contained video rectangle', () => {
    const perception = {layers: [{detections: [{type: 'VideoFrame', data: {originalWidth: 640, originalHeight: 480}}]}]};
    assert.deepEqual(findVideoFrame(perception), {originalWidth: 640, originalHeight: 480});
    assert.deepEqual(calculateContainedRect(1000, 500, 500, 500), {x: 250, y: 0, width: 500, height: 500});
    const mapper = createCoordinateMapper({canvasWidth: 1000, canvasHeight: 500, videoWidth: 500, videoHeight: 500, perception: {layers: []}});
    assert.deepEqual(mapper.point(250, 250), {x: 500, y: 250});
    assert.equal(mapper.lengthX(100), 100);
    const withMetadata = createCoordinateMapper({canvasWidth: 640, canvasHeight: 480, videoWidth: 1280, videoHeight: 960, perception});
    assert.deepEqual(withMetadata.point(320, 240), {x: 320, y: 240});
});

test('dispatcher isolates source, order, style and visibility without knowing the plugin', () => {
    const plugin = defineFeature({id: 'test-dispatch', label: 'Test', accepts: () => true,
        render({ctx}, resolved, settings) {
            ctx.strokeStyle = settings.color;
            ctx.strokeRect(resolved.items[0].data.x, 0, 10, 20);
        }});
    OVERLAY_FEATURES.push(plugin);
    try {
        const first = {payloadType: 'TestPayload', model: 'first', detections: [{data: {x: 1}}]};
        const second = {...first, model: 'second', detections: [{data: {x: 2}}]};
        const overlays = [select(plugin.id, first, {color: '#ff0000', opacity: 0.25}), select(plugin.id, second, {color: '#00ff00', opacity: 0.75})];
        const frame = {layers: [second, first]};
        const context = render(frame, overlays);
        assert.deepEqual(context.calls, [
            {rect: [1, 0, 10, 20], color: '#ff0000', alpha: 0.25},
            {rect: [2, 0, 10, 20], color: '#00ff00', alpha: 0.75},
        ]);
        assert.equal(context.globalAlpha, 1);
        assert.equal(render(frame, []).calls.length, 0);
        overlays[0].enabled = false;
        overlays[1].settings.opacity = 0;
        assert.equal(render(frame, overlays).calls.length, 0);
        overlays[0].enabled = true;
        assert.equal(render({layers: [second]}, overlays).calls.length, 0);
        assert.equal(render(null, overlays).calls.length, 0);
        // Background plugins run first even when added after annotations.
        const background = {...plugin, id: 'test-background', renderOrder: -1};
        OVERLAY_FEATURES.push(background);
        try {
            overlays[1].enabled = true;
            overlays[1].settings.opacity = 1;
            overlays[1].featureId = background.id;
            assert.deepEqual(render(frame, overlays).calls.map((call) => call.rect[0]), [2, 1]);
        } finally { OVERLAY_FEATURES.pop(); }
    } finally { OVERLAY_FEATURES.pop(); }
});
