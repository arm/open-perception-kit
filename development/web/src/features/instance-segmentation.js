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
import {imageBox} from '../image-geometry.js';
import {CLASS_CONTROLS, MASK_CONTROLS, MAX_MASK_PIXELS, classKey, classStyle, classLegend,
    classRenderSignature, withClassSettings, maskGeometryError, maskPlacement} from './segmentation.js';

const resolvedFrames = new WeakMap();
const surfaces = new WeakMap();
const MAX_TOTAL_MASK_PIXELS = 64 * 1024 * 1024;

function resolve(result) {
    if (resolvedFrames.has(result.output)) return resolvedFrames.get(result.output);
    const finish = value => { resolvedFrames.set(result.output, value); return value; };
    const invalid = (state, message) => finish({...result, items: [], state, message});
    const items = result.items.filter(item => item.type === 'InstanceSegmentationMask');
    if (items.length > 512) return invalid('unsupported', 'Too many instance masks (maximum 512).');
    const totalPixels = items.reduce((total, {data}) => total + (data.pixels instanceof Uint8Array ? data.pixels.length : 0), 0);
    if (totalPixels > MAX_TOTAL_MASK_PIXELS) return invalid('unsupported', `Instance masks exceed the total pixel budget (${MAX_TOTAL_MASK_PIXELS}).`);
    const validated = [];
    let frame, labelSet;
    for (const [position, item] of items.entries()) {
        const mask = item.data;
        const error = maskGeometryError(mask, 'InstanceSegmentationMask');
        if (error) return invalid(error.state, error.message);
        const {space, width, height} = mask;
        if (frame && (String(frame.frameId) !== String(space.frameId) || frame.width !== space.width || frame.height !== space.height))
            return invalid('ambiguous', 'Instance masks must share one declared source frame.');
        frame = space;
        if (mask.classId != null) {
            if (labelSet && labelSet !== mask.labelSet) return invalid('ambiguous', 'Instance class namespaces differ.');
            labelSet = mask.labelSet;
        }
        let left = width, right = -1, top = height, bottom = -1;
        // Validate the entire bitmap; supplied boxes are not a bound on its foreground.
        for (let y = 0, pixelIndex = 0; y < height; y++) {
            for (let x = 0; x < width; x++, pixelIndex++) {
                const value = mask.pixels[pixelIndex];
                if (value > 1) return invalid('unsupported', 'Instance masks require binary Uint8 pixels (0 or 1).');
                if (!value) continue;
                if (x < left) left = x;
                if (x > right) right = x;
                if (y < top) top = y;
                bottom = y;
            }
        }
        if (right >= 0) validated.push({...item, number: position + 1, bounds: {left, right, top, bottom}});
    }
    return finish({...result, items: validated, frame});
}

function colorize({data, bounds}, style) {
    const {width, height} = data;
    const rgba = new Uint8ClampedArray(width * height * 4);
    const coverage = new Uint8ClampedArray(rgba.length);
    const rgb = style.color.slice(1).match(/../g).map(hex => parseInt(hex, 16));
    const alpha = Math.round(style.opacity * 255);
    const edge = rgb[0] * 0.299 + rgb[1] * 0.587 + rgb[2] * 0.114 >= 128 ? 0 : 255;
    const fill = [...rgb, alpha];
    const outline = [edge, edge, edge, alpha];
    const pixels = data.pixels;
    for (let y = bounds.top; y <= bounds.bottom; y++) {
        const row = y * width;
        for (let x = bounds.left; x <= bounds.right; x++) {
            const i = row + x;
            if (!pixels[i]) continue;
            const boundary = x === 0 || x === width - 1
                || !pixels[i - 1] || !pixels[i + 1] || !pixels[i - width] || !pixels[i + width];
            const color = boundary ? outline : fill;
            const offset = i * 4;
            rgba[offset] = color[0];
            rgba[offset + 1] = color[1];
            rgba[offset + 2] = color[2];
            rgba[offset + 3] = color[3];
            coverage[offset + 3] = 255;
        }
    }
    return {rgba, coverage};
}

function render({ctx, mapper, overlay}, resolved, settings = resolved.settings) {
    if (!resolved.items?.length) return;
    const display = mapper.display;
    const dpr = ctx.getTransform().a;
    const pixelWidth = display.width * dpr, pixelHeight = display.height * dpr;
    if (![pixelWidth, pixelHeight].every(value => Number.isFinite(value) && value > 0)) return;
    const requestedWidth = Math.max(1, Math.round(pixelWidth));
    const requestedHeight = Math.max(1, Math.round(pixelHeight));
    // Compose at display-pixel resolution: a crop grid may be finer than its source-image region.
    // ponytail: cap very large displays; tile composition if pixel-exact output above this budget is needed.
    const scale = Math.min(1, 8192 / requestedWidth, 8192 / requestedHeight,
        Math.sqrt(MAX_MASK_PIXELS / requestedWidth / requestedHeight));
    const width = Math.max(1, Math.floor(requestedWidth * scale));
    const height = Math.max(1, Math.min(Math.floor(requestedHeight * scale), Math.floor(MAX_MASK_PIXELS / width)));
    const signature = classRenderSignature(settings);
    let surface = surfaces.get(overlay);
    if (!surface) {
        surface = {canvas: document.createElement('canvas'), raster: document.createElement('canvas')};
        surfaces.set(overlay, surface);
    }
    if (surface.output !== resolved.output || surface.signature !== signature
        || surface.canvas.width !== width || surface.canvas.height !== height) {
        if (surface.canvas.width !== width) surface.canvas.width = width;
        if (surface.canvas.height !== height) surface.canvas.height = height;
        const composite = surface.canvas.getContext('2d');
        composite.clearRect(0, 0, width, height);
        composite.imageSmoothingEnabled = false;
        for (const item of resolved.items) {
            const style = classStyle(item.data.classId, settings);
            if (!style.enabled || style.opacity <= 0) continue;
            const pixels = colorize(item, style);
            surface.raster.width = item.data.width;
            surface.raster.height = item.data.height;
            const raster = surface.raster.getContext('2d');
            const placement = maskPlacement(item.data.space, {x: 0, y: 0, width, height});
            // Replace only this instance's membership, preserving holes and non-accumulating alpha.
            raster.putImageData(new ImageData(pixels.coverage, item.data.width, item.data.height), 0, 0);
            composite.globalCompositeOperation = 'destination-out';
            composite.drawImage(surface.raster, ...placement);
            raster.putImageData(new ImageData(pixels.rgba, item.data.width, item.data.height), 0, 0);
            composite.globalCompositeOperation = 'source-over';
            composite.drawImage(surface.raster, ...placement);
        }
        surface.output = resolved.output;
        surface.signature = signature;
    }
    ctx.imageSmoothingEnabled = false;
    ctx.drawImage(surface.canvas, display.x, display.y, display.width, display.height);
}

function renderAnnotations({ctx, mapper}, resolved, settings = resolved.settings) {
    if (settings.boxes) {
        for (const {data, number} of resolved.items || []) {
            const style = classStyle(data.classId, settings);
            if (!style.enabled || style.opacity <= 0) continue;
            const box = data.box && imageBox(data.box);
            if (!box) continue;
            const x = Math.max(0, box.x), y = Math.max(0, box.y);
            const width = Math.min(box.frameWidth, box.x + box.width) - x;
            const height = Math.min(box.frameHeight, box.y + box.height) - y;
            if (width <= 0 || height <= 0) continue;
            ctx.save();
            ctx.globalAlpha *= style.opacity;
            drawObjectBox(ctx, {...box, x, y, width, height,
                label: `#${number} · ${style.label}${data.confidence == null ? '' : ` · ${Math.round(data.confidence * 100)}%`}`}, mapper, '#ffffff');
            ctx.restore();
        }
    }
}

export default withClassSettings(defineFeature({
    id: 'instance-segmentation', label: 'Instance segmentation', renderOrder: -1,
    accepts: source => source.payloadType === 'InstanceSegmentationMasks',
    controls: [...MASK_CONTROLS, {key: 'boxes', label: 'Boxes and labels', type: 'checkbox', value: true}],
    defaults: () => ({classes: {}}),
    classControls: CLASS_CONTROLS,
    resolve, render, renderAnnotations,
    legend(resolved, settings = resolved.settings) {
        const present = new Set((resolved.items || []).map(item => classKey(item.data.classId)));
        return classLegend(present, settings, resolved.message || (!present.size ? 'No instances in the displayed frame.' : ''));
    },
}), -1, 0xffffffff);
