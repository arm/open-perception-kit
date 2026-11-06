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
function smoothValue(value, previous, digits) {
    if (value === null || previous === null || !Number.isFinite(value) || !Number.isFinite(previous)) return value;
    return Number((previous * (1 - SMOOTHING_WEIGHT) + value * SMOOTHING_WEIGHT).toFixed(digits));
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

        const counts = new Map();
        for (const row of rows) if (row.key) counts.set(row.key, (counts.get(row.key) || 0) + 1);
        const identified = row => row.key && counts.get(row.key) === 1;
        const filtered = rows.map((row) => {
            const previous = identified(row) ? previousRows.get(row.key) : null;
            if (!previous || previous.unit !== row.unit) return {...row};
            const digits = row.unit === 'fps' ? 1 : 2;
            return {
                ...row,
                current: smoothValue(row.current, previous.current, digits),
                p95: smoothValue(row.p95, previous.p95, digits),
            };
        });
        previousRows = new Map(filtered.filter(identified).map((row) => [row.key, row]));
        lastDisplayAt = now;
        return filtered;
    };
}
