import { copyTextWithFeedback, setCopyButtonAvailable } from './copy-utils.js?v=icon-copy-buttons-20260608';

const body = document.getElementById('inferenceOutputBody');
const copyButton = document.getElementById('copyInferenceOutputBtn');
let currentLayers = [];
let latestOutput = null;

const number = (value, digits = 2) => {
    if (typeof value !== 'number' || !Number.isFinite(value)) return '';
    return value.toFixed(digits);
};

function detectionSummary(detection) {
    const type = detection?.type || 'Detection';
    const data = detection?.data || {};

    if (type === 'Rect') {
        const label = data.text || `Class ${data.classId ?? '-'}`;
        const confidence = number(data.confidence);
        return {
            title: label,
            detail: `box x ${number(data.x)} y ${number(data.y)} w ${number(data.width)} h ${number(data.height)}`,
            meta: confidence ? `${confidence}` : '',
        };
    }

    if (type === 'YawPitch') {
        return {
            title: 'Yaw / pitch',
            detail: `yaw ${number(data.yaw)} pitch ${number(data.pitch)}`,
            meta: number(data.confidence),
        };
    }

    if (type === 'Classification') {
        const best = Array.isArray(data.candidates) ? data.candidates[0] : null;
        return {
            title: best?.text || `Class ${best?.classId ?? '-'}`,
            detail: 'classification',
            meta: number(best?.confidence),
        };
    }

    if (type === 'LocalizedText') {
        return {
            title: data.text || 'Text',
            detail: `text x ${number(data.x)} y ${number(data.y)} w ${number(data.w)} h ${number(data.h)}`,
            meta: '',
        };
    }

    if (type === 'TrackTrace') {
        return {
            title: `Track ${data.trackId ?? '-'}`,
            detail: 'trace',
            meta: '',
        };
    }

    if (type === 'PersonClassification') {
        return {
            title: 'Person classification',
            detail: `yes ${number(data.yesConfidence)} no ${number(data.noConfidence)}`,
            meta: '',
        };
    }

    return {
        title: type,
        detail: Object.keys(data).slice(0, 4).join(', ') || 'output',
        meta: '',
    };
}

function layerTitle(layer) {
    return layer.model || layer.contentType || layer.engine || 'Layer';
}

function renderLayer(layer) {
    const detections = Array.isArray(layer.detections) ? layer.detections : [];
    const rows = detections.map(detectionSummary);
    const hiddenCount = Math.max(0, (layer.count || 0) - rows.length);

    return `
        <div class="inference-layer">
            <div class="inference-layer-header">
                <span class="inference-layer-title">${layerTitle(layer)}</span>
                <span class="inference-layer-count">${layer.count || 0}</span>
            </div>
            <div class="inference-layer-kind">${layer.contentType || layer.labelFamily || 'output'}</div>
            ${rows.length ? `
                <div class="inference-detections">
                    ${rows.map((row) => `
                        <div class="inference-detection">
                            <div class="inference-detection-main">
                                <span class="inference-detection-title">${row.title}</span>
                                ${row.meta ? `<span class="inference-detection-meta">${row.meta}</span>` : ''}
                            </div>
                            <div class="inference-detection-detail">${row.detail}</div>
                        </div>
                    `).join('')}
                </div>
            ` : '<div class="inference-output-empty">No detections</div>'}
            ${hiddenCount ? `<div class="inference-output-empty">${hiddenCount} more not shown</div>` : ''}
        </div>
    `;
}

export function renderInferenceOutput(output) {
    if (!body) return;

    latestOutput = output || latestOutput;
    if (window.PEK_FEED_PAUSED) {
        currentLayers = [];
        updateCopyButtonState();
        body.innerHTML = '<div class="inference-output-empty">Inference paused.</div>';
        return;
    }

    const filteredOutput = window.PEK_REGION_FILTER?.filterInferenceOutput
        ? window.PEK_REGION_FILTER.filterInferenceOutput(output)
        : output;

    const layers = Array.isArray(filteredOutput?.layers)
        ? filteredOutput.layers.filter((layer) => layer.model || layer.contentType || layer.engine)
        : [];
    currentLayers = layers;
    updateCopyButtonState();
    if (!layers.length) {
        body.innerHTML = '<div class="inference-output-empty">No inference output yet, enable a model to see inference here.</div>';
        return;
    }

    body.innerHTML = layers.map(renderLayer).join('');
}

function copyableLayers() {
    return currentLayers.filter((layer) => {
        const detections = Array.isArray(layer.detections) ? layer.detections : [];
        return detections.length > 0 || Number(layer.count || 0) > 0;
    });
}

function updateCopyButtonState() {
    setCopyButtonAvailable(copyButton, copyableLayers().length > 0);
}

function getInferenceText() {
    const layers = copyableLayers();
    if (!layers.length) return '';

    return layers.map((layer) => {
        const detections = Array.isArray(layer.detections) ? layer.detections : [];
        const rows = detections.map(detectionSummary);
        const lines = [
            `${layerTitle(layer)} (${layer.contentType || layer.labelFamily || 'output'}): ${layer.count || 0}`,
            ...rows.map((row) => {
                const meta = row.meta ? ` [${row.meta}]` : '';
                return `- ${row.title}${meta}: ${row.detail}`;
            }),
        ];

        return lines.join('\n');
    }).join('\n\n');
}

copyButton?.addEventListener('click', () => {
    copyTextWithFeedback(copyButton, getInferenceText());
});

window.addEventListener('ctrl-message', (event) => {
    if (event.detail?.inference_output) {
        renderInferenceOutput(event.detail.inference_output);
    }
});

window.addEventListener('feed-pause-change', () => {
    renderInferenceOutput(latestOutput);
});

window.addEventListener('regions-change', () => {
    renderInferenceOutput(latestOutput);
});

updateCopyButtonState();
