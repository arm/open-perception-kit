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

import {defineFeature} from '../overlay-feature.js';
import {CLASS_CONTROLS, MASK_CONTROLS, MAX_CLASSES, classStyle, classLegend, classRenderSignature, withClassSettings, maskGeometryError, maskValueReader, maskPlacement} from './segmentation.js';
const presentClasses = new WeakMap();
const surfaces = new WeakMap();

function classesIn(mask) {
    if (!mask) return new Set();
    if (!presentClasses.has(mask)) {
        // Hidden overlays still need presence, without allocating a raster.
        const seen = new Set();
        const sample = maskValueReader(mask);
        for (let i = 0; i < mask.width * mask.height; i++) {
            const id = sample(i);
            if (id !== mask.voidValue) seen.add(id);
            if (seen.size > MAX_CLASSES) break;
        }
        presentClasses.set(mask, seen);
    }
    return presentClasses.get(mask);
}

function colorize(mask, settings) {
    const colors = new Map([...classesIn(mask)].map(id => {
        const style = classStyle(id, settings);
        const rgb = style.color.slice(1).match(/../g).map(hex => parseInt(hex, 16));
        return [id, [...rgb, style.enabled ? Math.round(style.opacity * 255) : 0]];
    }));
    const rgba = new Uint8ClampedArray(mask.width * mask.height * 4);
    const sample = maskValueReader(mask);
    for (let i = 0; i < mask.width * mask.height; i++) {
        const id = sample(i);
        if (id === mask.voidValue) continue;
        const color = colors.get(id);
        const offset = i * 4;
        rgba[offset] = color[0];
        rgba[offset + 1] = color[1];
        rgba[offset + 2] = color[2];
        rgba[offset + 3] = color[3];
    }
    return rgba;
}

function resolve(result) {
    const items = result.items.filter(item => item.type === 'SemanticSegmentationMask');
    const invalid = (state, message) => ({...result, items: [], state, message});
    if (items.length > 1) return invalid('ambiguous', 'Expected one class map for this source.');
    if (!items.length) return {...result, items: []};
    const mask = items[0].data;
    const error = maskGeometryError(mask, 'SemanticSegmentationMask');
    if (error) return invalid(error.state, error.message);
    if (classesIn(mask).size > MAX_CLASSES) return invalid('unsupported', `Too many classes (maximum ${MAX_CLASSES}).`);
    return {...result, items, mask};
}

function render({ctx, mapper, overlay}, resolved, settings = resolved.settings) {
    if (!resolved.mask) return;
    const mask = resolved.mask;
    const signature = classRenderSignature(settings);
    let surface = surfaces.get(overlay);
    if (!surface) {
        surface = {canvas: document.createElement('canvas')};
        surfaces.set(overlay, surface);
    }
    if (surface.mask !== mask || surface.signature !== signature
        || surface.canvas.width !== mask.width || surface.canvas.height !== mask.height) {
        if (surface.canvas.width !== mask.width) surface.canvas.width = mask.width;
        if (surface.canvas.height !== mask.height) surface.canvas.height = mask.height;
        surface.canvas.getContext('2d').putImageData(new ImageData(colorize(mask, settings), mask.width, mask.height), 0, 0);
        surface.mask = mask;
        surface.signature = signature;
    }
    ctx.imageSmoothingEnabled = false;
    ctx.drawImage(surface.canvas, ...maskPlacement(mask.space, mapper.display));
}

export default withClassSettings(defineFeature({
    id: 'semantic-segmentation', label: 'Semantic segmentation', renderOrder: -1,
    accepts: source => source.payloadType === 'SemanticSegmentationMasks',
    controls: MASK_CONTROLS,
    defaults: () => ({classes: {}}),
    classControls: CLASS_CONTROLS,
    resolve, render,
    legend(resolved, settings = resolved.settings) {
        const present = new Set();
        classesIn(resolved.mask).forEach(id => present.add(id));
        const message = !resolved.mask ? resolved.message || 'No class map in the displayed frame.' : '';
        return classLegend(present, settings, message);
    },
}));
