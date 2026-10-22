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
import {objectId} from '../overlay-sources.js';

// Metadata supplies Uint8 class IDs, not class names or a semantic color map.
const palette = Array.from({length: 256}, (_, id) => {
    const rgb = [64 + (id & 7) * 24, 64 + ((id >> 3) & 7) * 24, 64 + (id >> 6) * 48];
    return {id, label: `Class ${id}`, rgb,
        color: '#' + rgb.map(value => value.toString(16).padStart(2, '0')).join('')};
});
const presentClasses = new WeakMap();
const surfaces = new WeakMap();

function classesIn(mask) {
    if (!mask) return new Set();
    if (!presentClasses.has(mask.pixels)) presentClasses.set(mask.pixels, new Set(mask.pixels));
    return presentClasses.get(mask.pixels);
}

function colorize(mask, classes) {
    const colors = palette.map(entry => {
        const style = classes[entry.id];
        const rgb = style ? style.color.slice(1).match(/../g).map(hex => parseInt(hex, 16)) : entry.rgb;
        return [...rgb, style ? (style.enabled ? Math.round(style.opacity * 255) : 0) : 255];
    });
    const rgba = new Uint8ClampedArray(mask.pixels.length * 4);
    for (let i = 0; i < mask.pixels.length; i++) {
        rgba.set(colors[mask.pixels[i]], i * 4);
    }
    return rgba;
}

function resolve(result, index) {
    const items = result.items.filter(item => item.type === 'SemanticSegmentationMask');
    const invalid = (state, message) => ({...result, items: [], state, message});
    if (items.length > 1) return invalid('ambiguous', 'Expected one full-frame class map for this source.');
    if (!items.length) return {...result, items};
    const mask = items[0].data;
    const {width, height, valueType, pixels, parentUuid} = mask;
    if (!Number.isInteger(width) || !Number.isInteger(height) || width <= 0 || height <= 0
        || width > 8192 || height > 8192 || width * height > 16_777_216
        || valueType !== 'Uint8' || !(pixels instanceof Uint8Array) || pixels.length !== width * height) {
        return invalid('unsupported', 'Invalid or oversized Uint8 class map.');
    }
    const parentId = objectId(parentUuid);
    const parents = parentId ? index.frame.layers.flatMap(layer => (layer.detections || [])
        .filter(item => objectId(item.data?.uuid) === parentId)
        .map(item => ({...item, payloadType: layer.payloadType}))) : [];
    if (parents.length !== 1 || parents[0].payloadType !== 'FrameContext' || parents[0].type !== 'VideoFrame') {
        return invalid('partial', 'Waiting for an unambiguous VideoFrame parent.');
    }
    const video = parents[0].data;
    if (video.originalWidth !== width || video.originalHeight !== height
        || ['sourceCropLeft', 'sourceCropRight', 'sourceCropTop', 'sourceCropBottom'].some(key => video[key])) {
        return invalid('unsupported', 'Requires a source-sized class map for an uncropped video frame.');
    }
    return {...result, items, mask};
}

function render({ctx, mapper, overlay}, resolved, settings = resolved.settings) {
    if (!resolved.mask) return;
    const mask = resolved.mask;
    const signature = JSON.stringify(Object.entries(settings.classes)
        .map(([id, style]) => [id, style.enabled, style.color, style.opacity]));
    let surface = surfaces.get(overlay);
    if (!surface) {
        surface = {canvas: document.createElement('canvas')};
        surfaces.set(overlay, surface);
    }
    if (surface.pixels !== mask.pixels || surface.signature !== signature
        || surface.canvas.width !== mask.width || surface.canvas.height !== mask.height) {
        if (surface.canvas.width !== mask.width) surface.canvas.width = mask.width;
        if (surface.canvas.height !== mask.height) surface.canvas.height = mask.height;
        surface.canvas.getContext('2d').putImageData(new ImageData(colorize(mask, settings.classes), mask.width, mask.height), 0, 0);
        surface.pixels = mask.pixels;
        surface.signature = signature;
    }
    const {x, y, width, height} = mapper.display;
    ctx.imageSmoothingEnabled = false;
    ctx.drawImage(surface.canvas, x, y, width, height);
}

const feature = defineFeature({
    id: 'semantic-segmentation', label: 'Semantic segmentation', renderOrder: -1,
    accepts: source => source.payloadType === 'SemanticSegmentationMasks' && source.contentType === 'semanticSegmentation',
    controls: [{key: 'opacity', label: 'Opacity', type: 'range', min: 0, max: 1, step: 0.01, value: 0.5}],
    defaults: () => ({classes: {}}),
    classControls: [{key: 'enabled', label: 'Visible', type: 'checkbox', switch: true, value: true}, ...STYLE_CONTROLS],
    resolve, render,
    legend(resolved, settings = resolved.settings) {
        const present = classesIn(resolved.mask);
        const entries = palette.filter(entry => present.has(entry.id) || settings?.classes?.[entry.id])
            .map(({id, label, color}) => ({id, color, enabled: true, opacity: 1,
                ...settings?.classes?.[id], label: settings?.classes?.[id]?.label?.trim() || label,
                present: present.has(id)}));
        const message = !resolved.mask ? resolved.message || 'No class map in the displayed frame.' : '';
        return {entries, message};
    },
});

export default {
    ...feature,
    validateSettings(settings) {
        const result = feature.validateSettings(settings);
        if (!result) return null;
        const classes = settings.classes === undefined ? {} : settings.classes;
        if (!classes || typeof classes !== 'object' || Array.isArray(classes)) return null;
        result.classes = {};
        for (const [id, style] of Object.entries(classes)) {
            if (!/^(0|[1-9]\d{0,2})$/.test(id) || Number(id) > 255
                || !style || typeof style.enabled !== 'boolean'
                || typeof style.color !== 'string' || !/^#[0-9a-f]{6}$/i.test(style.color)
                || (style.label !== undefined && (typeof style.label !== 'string' || style.label.length > 80))
                || !Number.isFinite(style.opacity) || style.opacity < 0 || style.opacity > 1) return null;
            result.classes[id] = {enabled: style.enabled, color: style.color, opacity: style.opacity};
            if (style.label?.trim()) result.classes[id].label = style.label.trim();
        }
        return result;
    },
};
