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

import { copyTextWithFeedback, setCopyButtonAvailable } from './copy-utils.js?v=icon-copy-buttons-20260608';
import {createPerformanceLowPass} from './performance-low-pass.js';
import {formatMetricValue, performanceRows} from './performance-data.js';

const body = document.getElementById('performanceMetricsBody');
const copyButton = document.getElementById('copyPerformanceMetricsBtn');

let currentRows = [];
const filterMetrics = createPerformanceLowPass();

function createCell(text, className) {
    const cell = document.createElement("td");
    if (className) {
        cell.className = className;
    }
    cell.textContent = String(text ?? "");
    return cell;
}

function renderEmptyRow() {
    const row = document.createElement("tr");
    const cell = createCell("No metrics yet", "performance-metrics-empty");
    cell.colSpan = 3;
    row.appendChild(cell);
    return row;
}

function renderMetricRow(metric) {
    const row = document.createElement("tr");
    row.appendChild(createCell(metric.stage));
    row.appendChild(createCell(formatMetricValue(metric.current, metric.unit)));
    row.appendChild(createCell(formatMetricValue(metric.p95, metric.unit)));
    return row;
}

function renderRows(rows) {
    if (!body) return;

    currentRows = rows;
    updateCopyButtonState();

    body.replaceChildren(...(rows.length ? rows.map(renderMetricRow) : [renderEmptyRow()]));
}

export function renderPerformanceMetrics(performance) {
    const measurements = Array.isArray(performance?.measurements) ? performance.measurements : [];
    const rows = filterMetrics(performanceRows(measurements));
    if (rows !== null) renderRows(rows);
}

function updateCopyButtonState() {
    setCopyButtonAvailable(copyButton, currentRows.length > 0);
}

function getMetricsText() {
    if (!currentRows.length) return '';

    return [
        'Stage\tCurrent\tP95',
        ...currentRows.map((row) => `${row.stage}\t${formatMetricValue(row.current, row.unit)}\t${formatMetricValue(row.p95, row.unit)}`),
    ].join('\n');
}

copyButton?.addEventListener('click', () => {
    copyTextWithFeedback(copyButton, getMetricsText());
});

window.addEventListener('metadata-message', (event) => {
    if (event.detail?.performance) {
        renderPerformanceMetrics(event.detail.performance);
    }
});

updateCopyButtonState();
