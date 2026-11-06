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
import {formatMetricValue, formatPerformanceLines, performanceRows, publishedPrecision} from '../src/performance-data.js';

test('performance values are smoothed at a readable cadence and reset when absent', () => {
    const filter = createPerformanceLowPass();
    const first = [
        {key: 'timing', stage: 'Inference', unit: 'ms', current: 10, p95: 20},
        {key: 'rate', stage: 'Pipeline', unit: 'fps', current: 30, p95: null},
    ];
    assert.deepEqual(filter(first, 0), first);
    assert.equal(filter([{key: 'timing', stage: 'Inference', unit: 'ms', current: 100, p95: 200}], 999), null);
    assert.deepEqual(filter([
        {key: 'timing', stage: 'Inference', unit: 'ms', current: 30, p95: 40},
        {key: 'rate', stage: 'Pipeline', unit: 'fps', current: 10, p95: null},
    ], 1000), [
        {key: 'timing', stage: 'Inference', unit: 'ms', current: 20, p95: 30},
        {key: 'rate', stage: 'Pipeline', unit: 'fps', current: 20, p95: null},
    ]);
    assert.deepEqual(filter([], 1001), []);
    assert.deepEqual(filter([{key: 'timing', stage: 'Inference', unit: 'ms', current: 30, p95: null}], 1002), [
        {key: 'timing', stage: 'Inference', unit: 'ms', current: 30, p95: null},
    ]);
});

test('structured measurements retain display precision and do not parse names or units', () => {
    const measurements = [{pixelFormat: 'BGRA', timings: [
        {name: 'Inference', meanMs: 10, p95Ms: 20},
        {name: 'Pipeline: ms (p95)', meanMs: 0, p95Ms: 0},
        {name: 'missing', meanMs: null},
        {name: 'invalid', meanMs: NaN},
        {name: 'infinite', meanMs: Infinity},
        {name: 'negative', meanMs: -1},
        {name: 'text', meanMs: '1.23ms'},
    ], frameRate: {fps: 30}}];
    const rows = performanceRows(measurements);
    assert.deepEqual(rows.map(row => [row.stage, formatMetricValue(row.current, row.unit), formatMetricValue(row.p95, row.unit)]), [
        ['Inference', '10.00ms', '20.00ms'],
        ['Pipeline: ms (p95)', '0.00ms', '0.00ms'],
        ['Pipeline', '30.0 FPS', ''],
    ]);
    assert.deepEqual(performanceRows([{frameRate: {fps: null}}]), []);
    assert.equal(formatMetricValue(performanceRows([{frameRate: {fps: 0}}])[0].current, 'fps'), '0.0 FPS');
});

test('formatting matches the former native producer, including exact binary ties', () => {
    assert.equal(publishedPrecision(1.125, 2), 1.12);
    assert.equal(publishedPrecision(1.375, 2), 1.38);
    assert.equal(publishedPrecision(2.675, 2), 2.67);
    assert.equal(publishedPrecision(1.25, 1), 1.2);
    assert.deepEqual(formatPerformanceLines({pixelFormat: 'BGRA',
        timings: [{name: 'OSD', meanMs: 1.23}], frameRate: {fps: 60}}), [
        'Pixel format            : BGRA',
        'OSD                     :    1.23ms',
        'Pipeline                :   60.0 FPS',
    ]);
    assert.deepEqual(formatPerformanceLines({frameRate: {fps: null}}), ['Pipeline                :    --.- FPS']);
    assert.deepEqual(formatPerformanceLines({}), []);
    const filter = createPerformanceLowPass();
    const producer = {component: 'test', instanceId: 'one'};
    filter(performanceRows([{producer, timings: [{scopeId: 'infer', name: 'Inference', meanMs: 1.12}]}]), 0);
    const [row] = filter(performanceRows([{producer, timings: [{scopeId: 'infer', name: 'Inference', meanMs: 1.14}]}]), 1000);
    assert.equal(formatMetricValue(row.current, row.unit), '1.13ms');
});

test('metric identity is independent of labels, order and publisher', () => {
    const producer = {component: 'test', instanceId: 'one'};
    const first = {scopeId: 'a', name: 'Inference', meanMs: 10};
    const second = {scopeId: 'b', name: 'Inference', meanMs: 100};
    const filter = createPerformanceLowPass();
    filter(performanceRows([{producer, timings: [first, second]}]), 0);
    const rows = filter(performanceRows([{producer, timings: [second, {...first, name: 'Renamed', meanMs: 30}]}]), 1000);
    assert.deepEqual(rows.map(row => row.current), [100, 20]);
    const other = filter(performanceRows([{producer: {...producer, instanceId: 'two'}, timings: [first]}]), 2000);
    assert.equal(other[0].current, 10);
});

test('missing or duplicate identities never merge measurement history', () => {
    for (const scopeId of ['', 'duplicate']) {
        const measurement = {producer: {component: 'test', instanceId: 'one'},
            timings: [{scopeId, name: 'Inference', meanMs: 10}, {scopeId, name: 'Inference', meanMs: 100}]};
        const filter = createPerformanceLowPass();
        filter(performanceRows([measurement]), 0);
        assert.deepEqual(filter(performanceRows([measurement]), 1000).map(row => row.current), [10, 100]);
    }
});

test('mean and p95 availability are independent, including valid zero', () => {
    const rows = performanceRows([{timings: [
        {name: 'p95 only', meanMs: null, p95Ms: 20},
        {name: 'mean only', meanMs: 0, p95Ms: null},
        {name: 'zero p95', meanMs: NaN, p95Ms: 0},
    ]}]);
    assert.deepEqual(rows.map(row => [row.current, row.p95]), [[null, 20], [0, null], [null, 0]]);
});

test('missing display labels do not hide measurements or change identity', () => {
    const value = {producer: {component: 'test', instanceId: 'one'}, timings: [
        {scopeId: 'inference', name: '', meanMs: 10, p95Ms: 20},
        {scopeId: 'p95', name: null, meanMs: null, p95Ms: 0},
        {name: '', meanMs: 0},
    ]};
    const rows = performanceRows([value]);
    assert.deepEqual(rows.map(row => [row.stage, row.current, row.p95]), [
        ['inference', 10, 20], ['p95', null, 0], ['Unnamed scope', 0, null],
    ]);
    assert.ok(rows[0].key);
    assert.equal(rows[2].key, null);
    const lines = formatPerformanceLines(value);
    assert.equal(lines.length, 3);
    assert.match(lines[0], /^inference\s+:\s+10\.00ms/);
    assert.match(lines[1], /^p95\s+:.*p95: 0\.00ms/);
    assert.match(lines[2], /^Unnamed scope\s+:\s+0\.00ms/);
});
