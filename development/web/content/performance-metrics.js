import { copyTextWithFeedback, setCopyButtonAvailable } from './copy-utils.js?v=icon-copy-buttons-20260608';

const body = document.getElementById('performanceMetricsBody');
const copyButton = document.getElementById('copyPerformanceMetricsBtn');

const METRIC_RE = /^(.*?)\s*:\s*([0-9.]+ms)\s*(?:\(p95:\s*([0-9.]+ms)\))?$/;
const FPS_RE = /^Pipeline\s*:?\s*([0-9.]+)\s+FPS$/;
let currentRows = [];

function parseMetricLine(line) {
    const text = String(line || '').replace(/[═]+/g, '').trim();
    if (!text) return null;

    const fps = text.match(FPS_RE);
    if (fps) {
        return { stage: 'Pipeline', current: `${fps[1]} FPS`, p95: '' };
    }

    const metric = text.match(METRIC_RE);
    if (metric) {
        return {
            stage: metric[1].trim(),
            current: metric[2],
            p95: metric[3] || '',
        };
    }

    return null;
}

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
    row.appendChild(createCell(metric.current));
    row.appendChild(createCell(metric.p95));
    return row;
}

function renderRows(rows) {
    if (!body) return;

    currentRows = rows;
    updateCopyButtonState();

    body.replaceChildren(...(rows.length ? rows.map(renderMetricRow) : [renderEmptyRow()]));
}

export function renderPerformanceMetrics(performance) {
    const lines = Array.isArray(performance?.lines) ? performance.lines : [];
    renderRows(lines.map(parseMetricLine).filter(Boolean));
}

function updateCopyButtonState() {
    setCopyButtonAvailable(copyButton, currentRows.length > 0);
}

function getMetricsText() {
    if (!currentRows.length) return '';

    return [
        'Stage\tCurrent\tP95',
        ...currentRows.map((row) => `${row.stage}\t${row.current}\t${row.p95}`),
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
