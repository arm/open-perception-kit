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

const DISPLAY_INTERVAL_MS = 1000;
const SMOOTHING_WEIGHT = 0.5;
const METRIC_VALUE = /^(\d+(?:\.\d+)?)(ms| FPS)$/;

function smoothValue(value, previous) {
    const currentMatch = METRIC_VALUE.exec(value);
    const previousMatch = METRIC_VALUE.exec(previous || '');
    if (!currentMatch || !previousMatch || currentMatch[2] !== previousMatch[2]) return value;

    const current = Number(currentMatch[1]);
    const old = Number(previousMatch[1]);
    const decimals = currentMatch[1].split('.')[1]?.length || 0;
    if (!Number.isFinite(current) || !Number.isFinite(old) || decimals > 10) return value;

    return `${(old * (1 - SMOOTHING_WEIGHT) + current * SMOOTHING_WEIGHT).toFixed(decimals)}${currentMatch[2]}`;
}

export function createPerformanceLowPass() {
    let lastDisplayAt = -Infinity;
    let previousRows = new Map();

    return (rows, now = performance.now()) => {
        if (!rows.length) {
            lastDisplayAt = -Infinity;
            previousRows.clear();
            return [];
        }
        if (now - lastDisplayAt < DISPLAY_INTERVAL_MS) return null;

        const filtered = rows.map((row) => {
            const previous = previousRows.get(row.stage);
            return {
                ...row,
                current: smoothValue(row.current, previous?.current),
                p95: smoothValue(row.p95, previous?.p95),
            };
        });
        previousRows = new Map(filtered.map((row) => [row.stage, row]));
        lastDisplayAt = now;
        return filtered;
    };
}
