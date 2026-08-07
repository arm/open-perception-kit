import { copyTextWithFeedback, setCopyButtonAvailable } from './copy-utils.js?v=icon-copy-buttons-20260608';

const body = document.getElementById('performanceMetricsBody');
const copyButton = document.getElementById('copyPerformanceMetricsBtn');

const MAX_METRIC_LINE_LENGTH = 240;
let currentRows = [];

function decimalText(value) {
    const parts = String(value || '').split('.');
    if (parts.length > 2 || parts.some((part) => part.length === 0)) {
        return false;
    }

    return parts.every((part) => [...part].every((char) => char >= '0' && char <= '9'));
}

function parseMillisToken(token) {
    if (!token.endsWith('ms')) {
        return '';
    }

    const value = token.slice(0, -2);
    return decimalText(value) ? `${value}ms` : '';
}

function parsePipelineFps(text) {
    if (!text.startsWith('Pipeline')) {
        return null;
    }

    const rest = text.slice('Pipeline'.length).trim();
    const valueText = (rest.startsWith(':') ? rest.slice(1) : rest).trim();
    if (!valueText.endsWith(' FPS')) {
        return null;
    }

    const fps = valueText.slice(0, -' FPS'.length).trim();
    return decimalText(fps) ? { stage: 'Pipeline', current: `${fps} FPS`, p95: '' } : null;
}

function parseP95Text(text) {
    if (!text) {
        return '';
    }
    if (!text.startsWith('(p95:') || !text.endsWith(')')) {
        return '';
    }

    return parseMillisToken(text.slice(5, -1).trim());
}

function parseTimedMetric(text) {
    const separator = text.indexOf(':');
    if (separator <= 0) {
        return null;
    }

    const stage = text.slice(0, separator).trim();
    const rest = text.slice(separator + 1).trim();
    const firstSpace = rest.indexOf(' ');
    const currentToken = firstSpace < 0 ? rest : rest.slice(0, firstSpace);
    const current = parseMillisToken(currentToken);
    if (!stage || !current) {
        return null;
    }

    const p95Text = firstSpace < 0 ? '' : rest.slice(firstSpace + 1).trim();
    return { stage, current, p95: parseP95Text(p95Text) };
}

function parseMetricLine(line) {
    const text = String(line || '').slice(0, MAX_METRIC_LINE_LENGTH).replaceAll('═', '').trim();
    if (!text) return null;

    return parsePipelineFps(text) || parseTimedMetric(text);
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
