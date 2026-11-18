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

import {STYLE_CONTROLS} from '../overlay-feature.js';
import {validMask, BitmapEncoding} from '../mask-data.js';

export const MAX_MASK_PIXELS = 16_777_216;
export const MAX_CLASSES = 4096;
// -1 is a saved UI style key only; absent SDK class IDs remain null.
export const classKey = id => id ?? -1;
export const CLASS_CONTROLS = [{key: 'enabled', label: 'Visible', type: 'checkbox', switch: true, value: true}, ...STYLE_CONTROLS];
export const MASK_CONTROLS = [{key: 'opacity', label: 'Opacity', type: 'range', min: 0, max: 1, step: 0.01, value: 0.5}];

export function classStyle(id, settings) {
    id = classKey(id);
    // Preserve Uint8 colors while folding wider instance class IDs into the same palette.
    const key = (id ^ (id >>> 8) ^ (id >>> 16) ^ (id >>> 24)) & 255;
    const rgb = [64 + (key & 7) * 24, 64 + ((key >> 3) & 7) * 24, 64 + (key >> 6) * 48];
    const style = settings?.classes?.[id];
    return {enabled: true, opacity: 1, color: '#' + rgb.map(value => value.toString(16).padStart(2, '0')).join(''),
        ...style, label: style?.label?.trim() || (id === -1 ? 'Unclassified' : `Class ${id}`)};
}

export function classLegend(present, settings, message = '') {
    const ids = new Set([...present, ...Object.keys(settings?.classes || {}).map(Number)]);
    return {entries: [...ids].sort((a, b) => a - b).map(id => ({id, ...classStyle(id, settings), present: present.has(id)})), message};
}

export function classRenderSignature(settings) {
    return JSON.stringify(Object.entries(settings.classes)
        .map(([id, style]) => [id, style.enabled, style.color, style.opacity]));
}

export function withClassSettings(feature, minId = 0, maxId = 0xffffffff) {
    return {...feature, validateSettings(settings) {
        const result = feature.validateSettings(settings);
        if (!result) return null;
        const classes = settings.classes === undefined ? {} : settings.classes;
        if (!classes || typeof classes !== 'object' || Array.isArray(classes) || Object.keys(classes).length > MAX_CLASSES) return null;
        result.classes = {};
        for (const [id, style] of Object.entries(classes)) {
            const value = Number(id);
            if (!Number.isInteger(value) || String(value) !== id || value < minId || value > maxId
                || !style || typeof style.enabled !== 'boolean'
                || typeof style.color !== 'string' || !/^#[0-9a-f]{6}$/i.test(style.color)
                || (style.label !== undefined && (typeof style.label !== 'string' || style.label.length > 80))
                || !Number.isFinite(style.opacity) || style.opacity < 0 || style.opacity > 1) return null;
            result.classes[id] = {enabled: style.enabled, color: style.color, opacity: style.opacity};
            if (style.label?.trim()) result.classes[id].label = style.label.trim();
        }
        return result;
    }};
}

export function maskGeometryError(mask, type) {
    const {width, height} = mask;
    if (!Number.isInteger(width) || !Number.isInteger(height) || width <= 0 || height <= 0
        || width > 8192 || height > 8192 || width * height > MAX_MASK_PIXELS
        || !validMask(mask, type)) {
        return {state: 'unsupported', message: 'Invalid or oversized mask, encoding, identity or image space.'};
    }
    return null;
}

export function maskValueReader(mask) {
    if (mask.encoding === BitmapEncoding.Uint8) return index => mask.pixels[index];
    const view = new DataView(mask.pixels.buffer, mask.pixels.byteOffset, mask.pixels.byteLength);
    return index => view.getUint32(index * 4, true);
}

export function maskPlacement(space, display) {
    return [display.x + space.originX * display.width / space.width,
        display.y + space.originY * display.height / space.height,
        space.extentWidth * display.width / space.width, space.extentHeight * display.height / space.height];
}
