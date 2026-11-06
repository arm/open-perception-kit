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

import {defineFeature, STYLE_CONTROLS} from '../overlay-feature.js';
import {drawTextChip} from '../overlay-drawing.js';
import {objectId} from '../overlay-sources.js';

const resolvedFrames = new WeakMap();
const labelLayouts = new WeakMap();
const validScore = value => Number.isFinite(value) && value >= 0 && value <= 1;

// Bound layout work without modifying the full text retained in the decoded payload.
function displayText(text) {
    let result = '';
    let count = 0;
    for (const character of text.trim()) {
        if (count++ === 512) { result += '…'; break; }
        result += character;
    }
    return result.replace(/\s+/gu, ' ').trim();
}

function resolve(result, index) {
    if (resolvedFrames.has(result.output)) return resolvedFrames.get(result.output);
    const regions = [];
    let missing = false;
    let ambiguous = false;
    let invalid = false;
    const geometry = data => {
        const frame = {originalWidth: data.frameWidth, originalHeight: data.frameHeight};
        const {x, y, width, height, confidence} = data;
        if (data.spatialValid !== true || !Number.isSafeInteger(frame.originalWidth) || !Number.isSafeInteger(frame.originalHeight)
            || frame.originalWidth <= 0 || frame.originalHeight <= 0
            || ![x, y, width, height].every(Number.isFinite) || width <= 0 || height <= 0
            || x >= frame.originalWidth || y >= frame.originalHeight
            || x + width <= 0 || y + height <= 0
            || (confidence != null && !validScore(confidence))) {
            invalid = true;
            return null;
        }
        const left = Math.max(0, x), top = Math.max(0, y);
        return {box: {x: left, y: top, width: Math.min(x + width, frame.originalWidth) - left, height: Math.min(y + height, frame.originalHeight) - top},
            frame, text: '', detectionScore: confidence, recognitionScore: undefined};
    };
    const isRecognition = result.output.source.payloadType === 'Classifications';
    const items = result.items.filter(item => item.type === (isRecognition ? 'Classification' : 'Rect'));
    if (items.length > 4096) {
        const failed = {...result, items: [], regions: [], state: 'unsupported', message: 'Too many OCR results (maximum 4096).'};
        resolvedFrames.set(result.output, failed);
        return failed;
    }
    if (isRecognition) {
        const groups = new Map();
        for (const item of items) {
            const id = objectId(item.data.parentUuid);
            if (!id) { missing = true; continue; }
            if (!groups.has(id)) groups.set(id, []);
            groups.get(id).push(item);
        }
        for (const [id, records] of groups) {
            const parents = (index.objects.get(id) || []).filter(item => item.source.payloadType === 'BoxDetections' && item.type === 'Rect');
            if (parents.length !== 1) {
                if (parents.length > 1) ambiguous = true;
                else missing = true;
                continue;
            }
            const region = geometry(parents[0].data);
            if (!region) continue;
            const candidates = records[0].data.candidates;
            if (records.length > 1 || (Array.isArray(candidates) && candidates.length > 1)) ambiguous = true;
            else if (!Array.isArray(candidates)) invalid = true;
            else if (candidates.length) {
                const candidate = candidates[0];
                if (!candidate || typeof candidate.text !== 'string'
                    || (candidate.confidence !== undefined && !validScore(candidate.confidence))) invalid = true;
                else {
                    region.text = displayText(candidate.text);
                    region.recognitionScore = candidate.confidence;
                }
            }
            regions.push(region);
        }
    } else {
        for (const {data} of items) {
            const region = geometry(data);
            if (!region) continue;
            if (data.text !== undefined && typeof data.text !== 'string') invalid = true;
            else region.text = displayText(data.text || '');
            regions.push(region);
        }
    }
    const resolved = {...result, items: regions, regions};
    if (ambiguous) Object.assign(resolved, {state: 'ambiguous', message: 'Ambiguous OCR relationships or candidates; unambiguous regions only, conflicting text omitted.'});
    else if (invalid) Object.assign(resolved, {state: regions.length ? 'partial' : 'unsupported', message: 'Invalid OCR geometry, text, or confidence; affected content omitted.'});
    else if (missing) Object.assign(resolved, {state: 'partial', message: 'Waiting for subject boxes with declared image geometry.'});
    resolvedFrames.set(result.output, resolved);
    return resolved;
}

function fitLabel(ctx, text, width) {
    if (ctx.measureText(text).width <= width) return text;
    if (ctx.measureText('…').width > width) return '';
    const characters = Array.from(text);
    let low = 0;
    let high = characters.length;
    while (low < high) {
        const middle = Math.ceil((low + high) / 2);
        if (ctx.measureText(characters.slice(0, middle).join('') + '…').width <= width) low = middle;
        else high = middle - 1;
    }
    return characters.slice(0, low).join('') + '…';
}

function fitLabels(ctx, regions, settings, width) {
    return regions.map(({text, recognitionScore, detectionScore}) => {
        const scores = [];
        if (settings.regionConfidence && validScore(detectionScore)) scores.push(`Region: ${(detectionScore * 100).toFixed(1)}%`);
        if (settings.textConfidence && validScore(recognitionScore)) scores.push(`Text: ${(recognitionScore * 100).toFixed(1)}%`);
        let label = settings.text ? text : '';
        const scoreLabel = scores.join(' · ');
        if (scoreLabel) {
            // Keep both requested scores visible when shortening a long recognized string.
            if (label) label = fitLabel(ctx, label, width - 8 - ctx.measureText(` · ${scoreLabel}`).width);
            label = [label, scoreLabel].filter(Boolean).join(' · ');
        }
        if (label) label = fitLabel(ctx, label, width - 8);
        return {label, width: label ? ctx.measureText(label).width + 8 : 0};
    });
}

function render({ctx, mapper, overlay}, resolved, settings) {
    const display = mapper.display;
    const fontSize = Number(settings.textSize);
    const labelHeight = fontSize + 4;
    ctx.save();
    ctx.beginPath();
    ctx.rect(display.x, display.y, display.width, display.height);
    ctx.clip();
    ctx.strokeStyle = settings.color;
    ctx.lineWidth = 2;
    ctx.font = `${fontSize}px monospace`;
    const signature = JSON.stringify([display.width, ctx.font, settings.text, settings.regionConfidence, settings.textConfidence]);
    let layout = labelLayouts.get(overlay);
    if (!layout || layout.regions !== resolved.regions || layout.signature !== signature) {
        // Retain only this overlay's current frame/layout, not a history of text strings.
        layout = {regions: resolved.regions, signature, labels: fitLabels(ctx, resolved.regions || [], settings, display.width)};
        labelLayouts.set(overlay, layout);
    }
    for (const [index, region] of (resolved.regions || []).entries()) {
        const {box, frame, text} = region;
        const x = display.x + box.x * display.width / frame.originalWidth;
        const y = display.y + box.y * display.height / frame.originalHeight;
        const width = box.width * display.width / frame.originalWidth;
        const height = box.height * display.height / frame.originalHeight;
        if (settings.regions) {
            ctx.setLineDash(text ? [] : [4, 3]);
            ctx.strokeRect(x, y, width, height);
        }
        const {label, width: labelWidth} = layout.labels[index];
        if (!label || display.height < labelHeight) continue;
        const labelX = Math.max(display.x, Math.min(x, display.x + display.width - labelWidth));
        const preferredY = y - labelHeight - 3 >= display.y ? y - labelHeight - 3 : y + height + 3;
        const labelY = Math.max(display.y, Math.min(preferredY, display.y + display.height - labelHeight));
        drawTextChip(ctx, label, labelX, labelY, fontSize);
    }
    ctx.restore();
}

const feature = defineFeature({
    id: 'ocr', label: 'OCR', requiresExplicitSource: true,
    accepts: source => ['BoxDetections', 'Classifications'].includes(source.payloadType),
    defaults: {color: '#00ffff'},
    controls: [
        {key: 'regions', label: 'Regions', type: 'checkbox', value: true},
        {key: 'text', label: 'Recognized text', type: 'checkbox', value: true},
        {key: 'regionConfidence', label: 'Region confidence', type: 'checkbox', value: false},
        {key: 'textConfidence', label: 'Text confidence', type: 'checkbox', value: false},
        ...STYLE_CONTROLS,
        {key: 'textSize', label: 'Text size', type: 'select', value: '16', options:
            [12, 16, 20, 24].map(size => ({value: String(size), label: `${size} px`}))},
    ],
    resolve, render,
});

export default {
    ...feature,
    validateSettings(settings) {
        // Preserve saved overlays that used the original single confidence switch.
        if (settings?.confidence === undefined) return feature.validateSettings(settings);
        if (typeof settings.confidence !== 'boolean') return null;
        return feature.validateSettings({regionConfidence: settings.confidence, textConfidence: settings.confidence, ...settings});
    },
};
