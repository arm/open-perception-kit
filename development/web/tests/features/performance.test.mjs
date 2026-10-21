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
import {render, select} from '../overlay-test-utils.mjs';

test('performance lines from payloads without provenance share one ordered text overlay', () => {
    const first = {payloadType: 'PerformanceOverlay', detections: [{type: 'PerformanceLine', data: {text: '30 FPS'}}]};
    const second = {...first, detections: [{type: 'PerformanceLine', data: {text: 'Inference: 10ms'}}]};
    const calls = render({layers: [first, second]}, [select('performance', first)]).calls;
    assert.deepEqual(calls.map((call) => call.text), ['30 FPS', 'Inference: 10ms']);
    assert.equal(calls[1].y - calls[0].y, 20);
});
