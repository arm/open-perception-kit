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
import {createPerformanceLowPass} from '../src/performance-low-pass.js';

test('performance values are smoothed at a readable cadence and reset when absent', () => {
    const filter = createPerformanceLowPass();
    const first = [
        {stage: 'Inference', current: '10.00ms', p95: '20.00ms'},
        {stage: 'Pipeline', current: '30.0 FPS', p95: ''},
    ];
    assert.deepEqual(filter(first, 0), first);
    assert.equal(filter([{stage: 'Inference', current: '100.00ms', p95: '200.00ms'}], 999), null);
    assert.deepEqual(filter([
        {stage: 'Inference', current: '30.00ms', p95: '40.00ms'},
        {stage: 'Pipeline', current: '10.0 FPS', p95: ''},
    ], 1000), [
        {stage: 'Inference', current: '20.00ms', p95: '30.00ms'},
        {stage: 'Pipeline', current: '20.0 FPS', p95: ''},
    ]);
    assert.deepEqual(filter([], 1001), []);
    assert.deepEqual(filter([{stage: 'Inference', current: '30.00ms', p95: ''}], 1002), [
        {stage: 'Inference', current: '30.00ms', p95: ''},
    ]);
});
