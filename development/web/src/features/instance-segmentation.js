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
import {drawObjectBox} from '../overlay-drawing.js';
import {objectId} from '../overlay-sources.js';
import {CLASS_CONTROLS, MASK_CONTROLS, MAX_MASK_PIXELS, classStyle, classLegend,
    classRenderSignature, withClassSettings, maskGeometryError} from './segmentation.js';

const resolvedFrames = new WeakMap();
const surfaces = new WeakMap();

function resolve(result, index) {
    if (resolvedFrames.has(result.output)) return resolvedFrames.get(result.output);
    const finish = value => { resolvedFrames.set(result.output, value); return value; };
    const invalid = (state, message) => finish({...result, items: [], masks: [], state, message});
    const items = result.items.filter(item => item.type === 'InstanceSegmentationMask');
    if (items.length > 512) return invalid('unsupported', 'Too many instance masks (maximum 512).');
    const masks = [];
    let totalPixels = 0;
    let parent;
    for (const [position, item] of items.entries()) {
        const mask = item.data;
        const error = maskGeometryError(mask, index);
        if (error) return invalid(error.state, error.message);
        totalPixels += mask.pixels.length;
        if (totalPixels > MAX_MASK_PIXELS) return invalid('unsupported', 'Instance masks exceed the total pixel budget.');
        const parentId = objectId(mask.parentUuid);
        if (parent && parent !== parentId) return invalid('ambiguous', 'Instance masks must share one VideoFrame parent.');
        parent = parentId;
        const {box, classId, confidence, width, height} = mask;
        if (!Number.isInteger(classId) || classId < -1 || classId > 2_147_483_647
            || !Number.isFinite(confidence) || confidence < 0 || confidence > 1
            || !box || ![box.x, box.y, box.width, box.height].every(Number.isFinite)
            || box.x < 0 || box.y < 0 || box.width <= 0 || box.height <= 0
            || box.x >= width || box.y >= height
            // Source coordinates/widths are independently rounded Float32 values on the wire.
            || box.x + box.width > width + 0.001 || box.y + box.height > height + 0.001) {
            return invalid('unsupported', 'Invalid instance class, confidence, or source-image box.');
        }
        let foreground = false;
        for (const value of mask.pixels) {
            if (value !== 0 && value !== 1) return invalid('unsupported', 'Instance masks require binary Uint8 pixels (0 or 1).');
            foreground ||= value === 1;
        }
        if (foreground) masks.push({...item, number: position + 1});
    }
    return finish({...result, items: masks, masks});
}

function colorize(masks, settings) {
    const {width, height} = masks[0].data;
    const rgba = new Uint8ClampedArray(width * height * 4);
    for (const {data} of masks) {
        const style = classStyle(data.classId, settings);
        if (!style.enabled || style.opacity <= 0) continue;
        const rgb = style.color.slice(1).match(/../g).map(hex => parseInt(hex, 16));
        const alpha = Math.round(style.opacity * 255);
        const edge = rgb[0] * 0.299 + rgb[1] * 0.587 + rgb[2] * 0.114 >= 128 ? 0 : 255;
        const fill = [...rgb, alpha];
        const outline = [edge, edge, edge, alpha];
        const pixels = data.pixels;
        for (let i = 0; i < pixels.length; i++) {
            if (!pixels[i]) continue;
            const boundary = i % width === 0 || i % width === width - 1
                || !pixels[i - 1] || !pixels[i + 1] || !pixels[i - width] || !pixels[i + width];
            // Later visible instances win overlaps; alpha does not accumulate per mask.
            rgba.set(boundary ? outline : fill, i * 4);
        }
    }
    return rgba;
}

function render({ctx, mapper, overlay}, resolved, settings = resolved.settings) {
    if (!resolved.masks?.length) return;
    const {width, height} = resolved.masks[0].data;
    const signature = classRenderSignature(settings);
    let surface = surfaces.get(overlay);
    if (!surface) {
        surface = {canvas: document.createElement('canvas')};
        surfaces.set(overlay, surface);
    }
    if (surface.output !== resolved.output || surface.signature !== signature) {
        if (surface.canvas.width !== width) surface.canvas.width = width;
        if (surface.canvas.height !== height) surface.canvas.height = height;
        surface.canvas.getContext('2d').putImageData(new ImageData(colorize(resolved.masks, settings), width, height), 0, 0);
        surface.output = resolved.output;
        surface.signature = signature;
    }
    const display = mapper.display;
    ctx.imageSmoothingEnabled = false;
    ctx.drawImage(surface.canvas, display.x, display.y, display.width, display.height);
    if (settings.boxes) {
        for (const {data, number} of resolved.masks) {
            const style = classStyle(data.classId, settings);
            if (!style.enabled || style.opacity <= 0) continue;
            ctx.save();
            ctx.globalAlpha *= style.opacity;
            drawObjectBox(ctx, {...data.box,
                width: Math.min(data.box.width, width - data.box.x), height: Math.min(data.box.height, height - data.box.y),
                label: `#${number} · ${style.label} · ${Math.round(data.confidence * 100)}%`}, mapper, '#ffffff');
            ctx.restore();
        }
    }
}

export default withClassSettings(defineFeature({
    id: 'instance-segmentation', label: 'Instance segmentation', renderOrder: -1,
    accepts: source => source.payloadType === 'InstanceSegmentationMasks' && source.contentType === 'instanceSegmentation',
    controls: [...MASK_CONTROLS, {key: 'boxes', label: 'Boxes and labels', type: 'checkbox', value: true}],
    defaults: () => ({classes: {}}),
    classControls: CLASS_CONTROLS,
    resolve, render,
    legend(resolved, settings = resolved.settings) {
        const present = new Set((resolved.masks || []).map(item => item.data.classId));
        return classLegend(present, settings, resolved.message || (!present.size ? 'No instances in the displayed frame.' : ''));
    },
}), -1, 2_147_483_647);
