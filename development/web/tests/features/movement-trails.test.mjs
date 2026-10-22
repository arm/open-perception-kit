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
import {render, select, trackerFixture} from '../overlay-test-utils.mjs';

test('movement trails draw separately and never change a tracked-object overlay', () => {
    const {tracks, trails} = trackerFixture();
    const frame = {layers: [tracks, trails]};
    const tracked = select('tracked-objects', tracks, {color: '#ff0000', opacity: 0.4});
    const standalone = select('movement-trails', trails, {color: '#00ff00', opacity: 0.8});
    assert.deepEqual(render(frame, [standalone]).calls, [{stroke: 'rgba(0, 255, 0, 1)', alpha: 0.8}]);
    for (const overlays of [[tracked, standalone], [standalone, tracked]]) {
        assert.equal(render(frame, overlays).calls.filter((call) => call.stroke).length, 2);
    }
    standalone.enabled = false;
    assert.equal(render(frame, [tracked, standalone]).calls.filter((call) => call.stroke).length, 1);
    const short = {...trails, detections: [{type: 'TrackTrace', data: {points: [{x: 1, y: 2}]}}]};
    assert.equal(render({layers: [short]}, [select('movement-trails', short)]).calls.length, 0);
});
