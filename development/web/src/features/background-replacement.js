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

// Resources belong to the overlay, not its source or latest frame. Removing the
// overlay releases them to GC; changing images drops the old decoded image.
const resources = new WeakMap();

function resolve(result, index) {
    const masks = result.items.filter((item) => item.type === 'SegmentationMask');
    if (result.output.layers.some((layer) => layer.compositingMode !== 'backgroundReplacement')) {
        return {...result, state: 'unsupported', message: 'Requires a background-opacity mask.'};
    }
    if (masks.length > 1) return {...result, items: [], state: 'ambiguous', message: 'Expected one full-frame mask.'};
    if (!masks.length) return {...result, items: []};
    const {width, height, valueType, pixels, parentUuid} = masks[0].data;
    if (!Number.isInteger(width) || !Number.isInteger(height) || width <= 0 || height <= 0
        || width > 8192 || height > 8192 || width * height > 16_777_216
        || valueType !== 'Uint8' || !(pixels instanceof Uint8Array) || pixels.length !== width * height) {
        return {...result, items: [], state: 'unsupported', message: 'Invalid or oversized Uint8 mask.'};
    }
    if (objectId(parentUuid)) {
        const parents = index.frame.layers.flatMap((layer) => layer.detections || [])
            .filter((item) => objectId(item.data?.uuid) === objectId(parentUuid));
        if (parents.length !== 1 || parents[0].type !== 'VideoFrame') {
            return {...result, items: [], state: 'partial', message: 'Waiting for unambiguous full-frame mask geometry.'};
        }
    }
    return {...result, items: masks};
}

function render({ctx, canvas, mapper, overlay}, resolved, settings) {
    if (!resolved.items.length) return;
    const mask = resolved.items[0].data;
    let cache = resources.get(overlay);
    if (!cache) {
        cache = {mask: document.createElement('canvas'), composite: document.createElement('canvas')};
        resources.set(overlay, cache);
    }
    if (cache.pixels !== mask.pixels) {
        cache.mask.width = mask.width;
        cache.mask.height = mask.height;
        const maskContext = cache.mask.getContext('2d');
        const data = maskContext.createImageData(mask.width, mask.height);
        for (let i = 0; i < mask.pixels.length; i++) data.data[i * 4 + 3] = mask.pixels[i];
        maskContext.putImageData(data, 0, 0);
        cache.pixels = mask.pixels;
    }
    const imageSource = settings.background === 'image' ? settings.image : '';
    if (cache.imageSource !== imageSource) {
        cache.imageSource = imageSource;
        cache.image = imageSource ? new Image() : null;
        if (cache.image) cache.image.src = imageSource;
    }
    const {x, y, width, height} = mapper.display;
    const dpr = canvas.width / Math.max(1, canvas.clientWidth);
    const w = Math.max(1, Math.round(width * dpr));
    const h = Math.max(1, Math.round(height * dpr));
    if (cache.composite.width !== w || cache.composite.height !== h) {
        cache.composite.width = w;
        cache.composite.height = h;
    }
    const composite = cache.composite.getContext('2d');
    composite.globalCompositeOperation = 'source-over';
    composite.clearRect(0, 0, w, h);
    composite.fillStyle = settings.color;
    composite.fillRect(0, 0, w, h);
    if (cache.image?.complete && cache.image.naturalWidth && cache.image.naturalHeight) {
        const scale = Math.max(w / cache.image.naturalWidth, h / cache.image.naturalHeight);
        const iw = cache.image.naturalWidth * scale;
        const ih = cache.image.naturalHeight * scale;
        composite.drawImage(cache.image, (w - iw) / 2, (h - ih) / 2, iw, ih);
    }
    // Clip only the replacement layer, leaving the original video underneath.
    // Intermediate bytes stay intermediate alpha; no binary threshold is applied.
    composite.globalCompositeOperation = 'destination-in';
    composite.drawImage(cache.mask, 0, 0, w, h);
    ctx.drawImage(cache.composite, x, y, width, height);
}

export default defineFeature({
    id: 'background-replacement', label: 'Background replacement', renderOrder: -1,
    accepts: (source) => source.payloadType === 'SegmentationMasks' && source.contentType === 'segmentation',
    controls: [
        {key: 'background', label: 'Background', type: 'select', value: 'color', options: [
            {value: 'color', label: 'Solid color'}, {value: 'image', label: 'Local image'},
        ]},
        ...STYLE_CONTROLS,
        {key: 'image', label: 'Background image', type: 'image', value: ''},
    ],
    defaults: {color: '#00ffff'}, resolve, render,
});
