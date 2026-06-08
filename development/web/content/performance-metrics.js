import { copyTextWithFeedback, setCopyButtonAvailable } from './copy-utils.js?v=icon-copy-buttons-20260608';

const section = document.querySelector('[data-performance-metrics-section]');
const toggle = document.getElementById('performanceMetricsToggle');
const panel = document.getElementById('performanceMetricsPanel');
const body = document.getElementById('performanceMetricsBody');
const copyButton = document.getElementById('copyPerformanceMetricsBtn');

const METRIC_RE = /^(.*?)\s*:\s*([0-9.]+ms)\s*(?:\(p95:\s*([0-9.]+ms)\))?$/;
const FPS_RE = /^Pipeline\s*:?\s*([0-9.]+)\s+FPS$/;
let currentRows = [];

function setExpanded(expanded) {
    if (!section || !toggle) return;

    section.classList.toggle('is-collapsed', !expanded);
    toggle.setAttribute('aria-expanded', expanded ? 'true' : 'false');
}

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

function renderRows(rows) {
    if (!body) return;

    currentRows = rows;
    updateCopyButtonState();

    if (!rows.length) {
        body.innerHTML = `
            <tr>
                <td colspan="3" class="performance-metrics-empty">No metrics yet</td>
            </tr>
        `;
        return;
    }

    body.innerHTML = rows.map((row) => `
        <tr>
            <td>${row.stage}</td>
            <td>${row.current}</td>
            <td>${row.p95}</td>
        </tr>
    `).join('');
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

if (toggle && panel) {
    toggle.addEventListener('click', () => {
        const expanded = toggle.getAttribute('aria-expanded') !== 'false';
        setExpanded(!expanded);
    });
}

copyButton?.addEventListener('click', () => {
    copyTextWithFeedback(copyButton, getMetricsText());
});

window.addEventListener('ctrl-message', (event) => {
    if (event.detail?.performance) {
        renderPerformanceMetrics(event.detail.performance);
    }
});

updateCopyButtonState();
