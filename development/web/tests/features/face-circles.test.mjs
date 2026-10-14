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
import {render, select, faceFixture} from '../overlay-test-utils.mjs';

test('face circles selects only face regions and retains their circle geometry and styling', () => {
    const face = faceFixture();
    const tracked = {...face, payloadType: 'ObjectTracks'};
    const nonFace = {...face, contentType: 'genericObject'};
    const invalid = {...face, payloadType: 'PoseEstimations'};
    const catalog = new Map([face, tracked, nonFace, invalid].map((layer) => [sourceKey(outputSource(layer)), outputSource(layer)]));
    const feature = featureFor('face-circles');
    assert.deepEqual(feature.discover(catalog), [outputSource(face), outputSource(tracked)]);
    assert.equal(feature.defaults(outputSource(face)).color, '#2600ff');
    for (const layer of [face, tracked]) {
        assert.deepEqual(render({layers: [layer]}, [select(feature.id, layer, {color: '#123456', opacity: 0.4})]).calls, [
            {arc: [25, 40, 15, 0, Math.PI * 2]}, {stroke: '#123456', alpha: 0.4},
        ]);
    }
    assert.equal(render({layers: [nonFace]}, [select(feature.id, nonFace)]).calls.length, 0);
});
