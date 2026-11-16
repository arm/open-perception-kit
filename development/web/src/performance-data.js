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

const available = value => typeof value === 'number' && Number.isFinite(value) && value >= 0;
const scopeLabel = timing => typeof timing.name === 'string' && timing.name.length ? timing.name
    : typeof timing.scopeId === 'string' && timing.scopeId.length ? timing.scopeId : 'Unnamed scope';

// Preserve the producer's previous std::format precision before browser smoothing.
// toFixed rounds exact ties away from zero; std::format uses ties-to-even.
export function publishedPrecision(value, digits) {
    const scale = 10 ** digits;
    const scaled = value * scale;
    const lower = Math.floor(scaled);
    if (Number.isInteger(value * 2 ** (digits + 1)) && scaled - lower === 0.5 && lower % 2 === 0) {
        return lower / scale;
    }
    return Number(value.toFixed(digits));
}

export function performanceRows(measurements) {
    const rows = [];
    for (const measurement of measurements || []) {
        const producer = measurement.producer;
        const key = (kind, id) => producer?.component && producer?.instanceId && id
            ? JSON.stringify([producer.component, producer.instanceId, kind, id]) : null;
        for (const timing of measurement.timings || []) {
            if (!timing) continue;
            rows.push({key: key('timing', timing.scopeId), stage: scopeLabel(timing), unit: 'ms',
                current: available(timing.meanMs) ? publishedPrecision(timing.meanMs, 2) : null,
                p95: available(timing.p95Ms) ? publishedPrecision(timing.p95Ms, 2) : null});
        }
        if (measurement.frameRate) {
            rows.push({key: key('frameRate', 'fps'), stage: 'Pipeline', unit: 'fps',
                current: available(measurement.frameRate.fps) ? publishedPrecision(measurement.frameRate.fps, 1) : null, p95: null});
        }
    }
    // Unavailable duplicate observations still make a scope's history ambiguous.
    const counts = new Map();
    for (const row of rows) if (row.key) counts.set(row.key, (counts.get(row.key) || 0) + 1);
    for (const row of rows) if (counts.get(row.key) > 1) row.key = null;
    return rows.filter(row => row.current !== null || row.p95 !== null);
}

export function formatMetricValue(value, unit) {
    if (!available(value)) return '';
    return unit === 'fps' ? `${value.toFixed(1)} FPS` : `${value.toFixed(2)}ms`;
}

// Text exists only at the rendering boundary; it is never parsed back into data.
export function formatPerformanceLines(measurement) {
    const rows = [];
    if (measurement.pixelFormat) rows.push(['Pixel format', measurement.pixelFormat]);
    for (const timing of measurement.timings || []) {
        if (!timing) continue;
        let value = available(timing.meanMs)
            ? `${publishedPrecision(timing.meanMs, 2).toFixed(2).padStart(7)}ms` : '  --.--ms';
        if (available(timing.p95Ms)) value += ` (p95: ${publishedPrecision(timing.p95Ms, 2).toFixed(2)}ms)`;
        rows.push([scopeLabel(timing), value]);
    }
    if (measurement.frameRate) {
        const fps = measurement.frameRate.fps;
        rows.push(['Pipeline', available(fps)
            ? `${publishedPrecision(fps, 1).toFixed(1).padStart(6)} FPS` : '   --.- FPS']);
    }
    return rows.map(([name, value]) => `${name.padEnd(24)}: ${value}`);
}
