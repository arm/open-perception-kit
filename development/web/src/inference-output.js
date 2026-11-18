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
import {collectOutputs} from './overlay-sources.js';
import {contactOutcome, validCandidate, validEmptyClassification, presenceOutcome} from './classification-results.js';
import {validGaze} from './gaze-results.js';

const body = document.getElementById('inferenceOutputBody');
const copyButton = document.getElementById('copyInferenceOutputBtn');
let currentLayers = [];
let latestOutput = null;
const audioAssessments = new Map();
let disabledModels = new Set();

const isAudioAssessment = layer => ['VoiceActivityDetections', 'TurnDetections'].includes(layer.payloadType);

function renderWithAudio(layers) {
    renderInferenceOutput({layers: [
        ...layers.filter(layer => !isAudioAssessment(layer)),
        ...[...audioAssessments.values()].flatMap(output => output.layers),
    ]});
}

function resetAudioAssessments() {
    audioAssessments.clear();
    renderWithAudio(latestOutput?.layers || []);
}

const number = (value, digits = 2) => {
    if (typeof value !== 'number' || !Number.isFinite(value)) return '';
    return value.toFixed(digits);
};

function detectionSummary(detection) {
    const type = detection?.type || 'Detection';
    const data = detection?.data || {};

    if (type === 'Rect') {
        const label = data.displayText || data.text || `Class ${data.classId ?? '-'}`;
        const confidence = number(data.confidence);
        return {
            title: label,
            detail: data.spatialValid === false ? 'Spatial reference unavailable'
                : `box x ${number(data.x)} y ${number(data.y)} w ${number(data.width)} h ${number(data.height)}`,
            meta: confidence ? `${confidence}` : '',
        };
    }

    if (type === 'GazeMeasurement') {
        return {
            title: 'Yaw / pitch',
            detail: validGaze(data) ? `yaw ${number(data.angles.yawDegrees)}° pitch ${number(data.angles.pitchDegrees)}°` : 'Gaze unavailable',
            meta: validGaze(data) ? number(data.confidence) : '',
        };
    }

    if (type === 'CameraContact') {
        if (contactOutcome(data) === null)
            return {title: 'Contact unavailable', detail: 'classification', meta: ''};
        const result = data.result;
        return {title: result.inContact ? 'Contact' : 'No contact',
            detail: 'classification', meta: number(result?.confidence)};
    }

    if (type === 'Classification') {
        const best = Array.isArray(data.candidates) ? data.candidates[0] : null;
        if (!best) return {title: validEmptyClassification(data) ? 'No candidates' : 'Classification unavailable',
            detail: validEmptyClassification(data) ? 'Empty classification result' : 'Missing classification kind or identity', meta: ''};
        if (best && !validCandidate(data, best))
            return {title: 'Classification unavailable', detail: 'Invalid kind, identity or score', meta: ''};
        return {
            title: best?.text || `Class ${best?.classId ?? '-'}`,
            detail: 'classification',
            meta: number(best?.confidence),
        };
    }

    if (type === 'VoiceActivityDetection' || type === 'TurnDetection') {
        if (!data.usable) return {title: 'Audio result unavailable', detail: 'Missing or invalid audio measurement', meta: ''};
        const voiceActivity = type === 'VoiceActivityDetection';
        return {
            title: voiceActivity ? (data.speechActive ? 'Speech active' : 'Silence')
                : (data.turnComplete ? 'Turn assessment: complete' : 'Turn assessment: ongoing'),
            detail: `audio samples [${data.windowStartSample}, ${data.windowEndSample})${data.discontinuity ? '; stream reset' : ''}`,
            meta: number(voiceActivity ? data.speechProbability : data.turnProbability),
        };
    }

    if (type === 'LocalizedText') {
        return {
            title: data.text || 'Text',
            detail: `text x ${number(data.x)} y ${number(data.y)} w ${number(data.w)} h ${number(data.h)}`,
            meta: '',
        };
    }

    if (type === 'AudioFrame') return {title: 'Audio source buffer',
        detail: `${data.originalChannels} channels, ${data.originalFrequency} Hz`, meta: `${data.originalSampleCount} samples`};

    if (type === 'TrackTrace') {
        return {
            title: `Track ${data.identity?.trackId ?? '-'}`,
            detail: data.spatialValid === false ? 'Spatial reference unavailable' : 'trace',
            meta: '',
        };
    }

    if (type === 'PersonClassification') {
        return {
            title: 'Person classification',
            detail: presenceOutcome(data) === null ? 'Presence unavailable'
                : `${data.result.personPresent ? 'Person' : 'No person'}; raw yes ${number(data.result.yesScore)} no ${number(data.result.noScore)}`,
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
    return layer.model || layer.payloadType || layer.engine || 'Layer';
}

function createNode(tag, className, text) {
    const node = document.createElement(tag);
    if (className) {
        node.className = className;
    }
    if (text !== undefined) {
        node.textContent = String(text);
    }
    return node;
}

function renderEmpty(message) {
    return createNode('div', 'inference-output-empty wfr-txt-empty-state', message);
}

function renderDetection(row) {
    const item = createNode('div', 'inference-detection');
    const main = createNode('div', 'inference-detection-main');

    main.appendChild(createNode('span', 'inference-detection-title wfr-txt-label--sm', row.title));
    if (row.meta) {
        main.appendChild(createNode('span', 'inference-detection-meta wfr-txt-caption', row.meta));
    }

    item.appendChild(main);
    item.appendChild(createNode('div', 'inference-detection-detail wfr-txt-caption', row.detail));
    return item;
}

function renderLayer(layer) {
    const detections = Array.isArray(layer.detections) ? layer.detections : [];
    const rows = detections.map(detectionSummary);
    const hiddenCount = Math.max(0, (layer.count || 0) - rows.length);
    const container = createNode('div', 'inference-layer');
    const header = createNode('div', 'inference-layer-header');

    header.appendChild(createNode('span', 'inference-layer-title wfr-txt-label', layerTitle(layer)));
    header.appendChild(createNode('span', 'inference-layer-count wfr-txt-caption', layer.status === 'invalid' ? 'Unavailable' : layer.count || 0));
    container.appendChild(header);
    container.appendChild(createNode('div', 'inference-layer-kind wfr-txt-caption', layer.payloadType || 'output'));

    if (rows.length) {
        const detectionsNode = createNode('div', 'inference-detections');
        rows.forEach((row) => detectionsNode.appendChild(renderDetection(row)));
        container.appendChild(detectionsNode);
    } else {
        container.appendChild(renderEmpty(layer.status === 'invalid' ? 'Invalid result data' : 'No results'));
    }

    if (hiddenCount) {
        container.appendChild(renderEmpty(`${hiddenCount} more not shown`));
    }

    return container;
}

export function renderInferenceOutput(output) {
    if (!body) return;

    latestOutput = output || latestOutput;
    if (window.OPK_FEED_PAUSED) {
        currentLayers = [];
        updateCopyButtonState();
        body.replaceChildren(renderEmpty("Inference paused."));
        return;
    }

    const layers = Array.isArray(output?.layers)
        ? output.layers.filter((layer) => layer.payloadType)
        : [];
    currentLayers = layers;
    updateCopyButtonState();
    if (output?.decodeError) {
        body.replaceChildren(renderEmpty(`Metadata unavailable: ${output.decodeError}`));
        return;
    }
    const unsupported = output?.unsupportedPayloadCount > 0
        ? [renderEmpty(`${output.unsupportedPayloadCount} unsupported or malformed payload(s) were not interpreted.`)] : [];
    if (!layers.length && !unsupported.length) {
        body.replaceChildren(renderEmpty("No inference output yet, enable a model to see inference here."));
        return;
    }

    body.replaceChildren(...unsupported, ...layers.map(renderLayer));
}

function copyableLayers() {
    return currentLayers.filter((layer) => {
        const detections = Array.isArray(layer.detections) ? layer.detections : [];
        return layer.status === 'empty' || layer.status === 'invalid' || detections.length > 0;
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
            `${layerTitle(layer)} (${layer.payloadType || 'output'}): ${layer.count || 0}`,
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

window.addEventListener('metadata-message', (event) => {
    const layers = event.detail?.inference_output?.layers;
    if (!Array.isArray(layers)) return;
    if (window.OPK_FEED_PAUSED || event.detail.decode_error) {
        audioAssessments.clear();
    } else {
        const resetElements = new Set(layers.filter(layer =>
            isAudioAssessment(layer) && layer.detections?.some(item => item.data?.discontinuity))
            .map(layer => layer.inferElementId || ''));
        for (const [key, output] of audioAssessments)
            if (resetElements.has(output.source.inferElementId)) audioAssessments.delete(key);
    }
    if (!window.OPK_FEED_PAUSED && !event.detail.decode_error)
        for (const [key, output] of collectOutputs({layers}))
            if (isAudioAssessment(output.source) && !disabledModels.has(output.source.inferElementId))
                audioAssessments.set(key, output);
    renderWithAudio(layers);
});

window.addEventListener('models-change', (event) => {
    disabledModels = new Set(event.detail.filter(model => !model.active).map(model => model.element_name));
    for (const [key, output] of audioAssessments)
        if (disabledModels.has(output.source.inferElementId)) audioAssessments.delete(key);
    renderWithAudio(latestOutput?.layers || []);
});

window.addEventListener('metadata-connection', (event) => {
    if (!event.detail.connected) resetAudioAssessments();
});

window.addEventListener('microphone-state', (event) => {
    if (!event.detail.active) resetAudioAssessments();
});

window.addEventListener('feed-pause-change', () => {
    if (window.OPK_FEED_PAUSED) resetAudioAssessments();
    else renderInferenceOutput(latestOutput);
});


updateCopyButtonState();
