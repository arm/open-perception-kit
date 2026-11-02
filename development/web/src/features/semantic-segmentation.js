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
import {CLASS_CONTROLS, MASK_CONTROLS, classStyle, classLegend, classRenderSignature, withClassSettings, maskGeometryError} from './segmentation.js';
const presentClasses = new WeakMap();
const surfaces = new WeakMap();

function classesIn(mask) {
    if (!mask) return new Uint8Array(256);
    if (!presentClasses.has(mask.pixels)) {
        // Hidden overlays still need presence, without allocating a raster.
        const seen = new Uint8Array(256);
        for (let i = 0; i < mask.pixels.length; i++) seen[mask.pixels[i]] = 1;
        presentClasses.set(mask.pixels, seen);
    }
    return presentClasses.get(mask.pixels);
}

function colorize(mask, settings) {
    const colors = Array.from({length: 256}, (_, id) => {
        const style = classStyle(id, settings);
        const rgb = style.color.slice(1).match(/../g).map(hex => parseInt(hex, 16));
        return [...rgb, style.enabled ? Math.round(style.opacity * 255) : 0];
    });
    const rgba = new Uint8ClampedArray(mask.pixels.length * 4);
    const seen = presentClasses.get(mask.pixels) || new Uint8Array(256);
    for (let i = 0; i < mask.pixels.length; i++) {
        const id = mask.pixels[i];
        const color = colors[id];
        const offset = i * 4;
        rgba[offset] = color[0];
        rgba[offset + 1] = color[1];
        rgba[offset + 2] = color[2];
        rgba[offset + 3] = color[3];
        seen[id] = 1;
    }
    presentClasses.set(mask.pixels, seen);
    return rgba;
}

function resolve(result, index) {
    const items = result.items.filter(item => item.type === 'SemanticSegmentationMask');
    const invalid = (state, message) => ({...result, items: [], state, message});
    if (items.length > 1) return invalid('ambiguous', 'Expected one full-frame class map for this source.');
    if (!items.length) return invalid('partial', 'No class map in the displayed frame.');
    const mask = items[0].data;
    const error = maskGeometryError(mask, index);
    if (error) return invalid(error.state, error.message);
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
    if (surface.pixels !== mask.pixels || surface.signature !== signature
        || surface.canvas.width !== mask.width || surface.canvas.height !== mask.height) {
        if (surface.canvas.width !== mask.width) surface.canvas.width = mask.width;
        if (surface.canvas.height !== mask.height) surface.canvas.height = mask.height;
        surface.canvas.getContext('2d').putImageData(new ImageData(colorize(mask, settings), mask.width, mask.height), 0, 0);
        surface.pixels = mask.pixels;
        surface.signature = signature;
    }
    const {x, y, width, height} = mapper.display;
    ctx.imageSmoothingEnabled = false;
    ctx.drawImage(surface.canvas, x, y, width, height);
}

export default withClassSettings(defineFeature({
    id: 'semantic-segmentation', label: 'Semantic segmentation', renderOrder: -1,
    accepts: source => source.payloadType === 'SemanticSegmentationMasks' && source.contentType === 'semanticSegmentation',
    controls: MASK_CONTROLS,
    defaults: () => ({classes: {}}),
    classControls: CLASS_CONTROLS,
    resolve, render,
    legend(resolved, settings = resolved.settings) {
        const present = new Set();
        classesIn(resolved.mask).forEach((seen, id) => { if (seen) present.add(id); });
        const message = !resolved.mask ? resolved.message || 'No class map in the displayed frame.' : '';
        return classLegend(present, settings, message);
    },
}));
