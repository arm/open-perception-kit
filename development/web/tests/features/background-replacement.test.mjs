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

import assert from 'node:assert/strict';
import test from 'node:test';
import background from '../../src/features/background-replacement.js';
import {resolveOverlay} from '../../src/overlay-features.js';
import {indexFrame} from '../../src/overlay-sources.js';
import {loadOverlays, saveOverlays} from '../../src/overlays.js';
import {loadLocalImage, validLocalImage} from '../../src/overlay-image.js';
import {select} from '../overlay-test-utils.mjs';

function fixture() {
    return {payloadType: 'SegmentationMasks', contentType: 'segmentation', model: 'RVM',
        compositingMode: 'backgroundReplacement', detections: [{type: 'SegmentationMask', data: {
            width: 3, height: 1, valueType: 'Uint8', pixels: new Uint8Array([0, 128, 255]), parentUuid: '42',
        }}]};
}
const video = {payloadType: 'FrameContext', detections: [{type: 'VideoFrame', data: {uuid: '42'}}]};

test('background replacement requires explicit opacity semantics and one valid full-frame mask', () => {
    const layer = fixture();
    const overlay = select(background.id, layer);
    const resolve = (...layers) => resolveOverlay(indexFrame({layers}), overlay);
    assert.equal(resolve(video, layer).state, 'ready');
    assert.equal(resolve({...layer, model: 'Other RVM'}).state, 'waiting');
    assert.equal(resolve(layer).state, 'partial');
    assert.equal(resolve(video, video, layer).state, 'partial');
    assert.equal(resolve(video, layer, layer).state, 'ambiguous');
    assert.equal(resolve(video, {...layer, compositingMode: 'classLabels'}).state, 'unsupported');
    assert.equal(resolve(video, {...layer, detections: []}).items.length, 0);
    for (const changes of [{width: 8193}, {height: -1}, {valueType: 'Uint32'},
        {pixels: [0, 128, 255]}, {pixels: new Uint8Array(2)}]) {
        const invalid = {...layer, detections: [{type: 'SegmentationMask', data: {...layer.detections[0].data, ...changes}}]};
        assert.equal(resolve(video, invalid).state, 'unsupported');
    }
});

test('background settings persist local rasters and reject remote URLs and invalid modes', () => {
    const overlay = select(background.id, fixture(), {background: 'image', image: 'data:image/png;base64,AAAA'});
    let saved;
    const storage = {setItem: (_key, value) => { saved = value; }, getItem: () => saved};
    assert.equal(saveOverlays(storage, [overlay]), '');
    assert.deepEqual(loadOverlays(storage).overlays, [overlay]);
    for (const image of ['https://example.com/image.jpg', 'file:///image.png', 'data:image/svg+xml;base64,AAAA', 'data:image/png;base64,' + 'A'.repeat(2_000_000)]) {
        assert.equal(background.validateSettings({...overlay.settings, image}), null);
    }
    assert.equal(background.validateSettings({...overlay.settings, background: 'video'}), null);
});

test('mask alpha is preserved, cached per overlay, and drawn only inside the displayed video', () => {
    const original = globalThis.document;
    const canvases = [];
    globalThis.document = {createElement() {
        const context = {clearRect() {}, fillRect() {}, drawImage() {},
            createImageData: (w, h) => ({data: new Uint8ClampedArray(w * h * 4)}),
            putImageData(data) { this.pixels = [...data.data]; }};
        const canvas = {getContext: () => context};
        canvases.push(canvas);
        return canvas;
    }};
    try {
        const layer = fixture();
        const overlay = select(background.id, layer);
        const resolved = resolveOverlay(indexFrame({layers: [video, layer]}), overlay);
        const draws = [];
        const target = {overlay, ctx: {drawImage: (...args) => draws.push(args)},
            canvas: {width: 200, clientWidth: 100}, mapper: {display: {x: 10, y: 20, width: 90, height: 50}}};
        background.render(target, resolved, overlay.settings);
        background.render(target, resolved, overlay.settings);
        assert.equal(canvases.length, 2);
        assert.deepEqual(canvases[0].getContext().pixels, [0, 0, 0, 0, 0, 0, 0, 128, 0, 0, 0, 255]);
        assert.equal(canvases[1].width, 180);
        assert.equal(canvases[1].height, 100);
        assert.equal(canvases[1].getContext().globalCompositeOperation, 'destination-in');
        assert.deepEqual(draws[0].slice(1), [10, 20, 90, 50]);
        background.render({...target, overlay: {...overlay}}, resolved, overlay.settings);
        assert.equal(canvases.length, 4);
    } finally {
        if (original === undefined) delete globalThis.document;
        else globalThis.document = original;
    }
});

test('local image preparation bounds dimensions, rejects invalid input and closes decoded bitmaps', async () => {
    const originals = {document: globalThis.document, createImageBitmap: globalThis.createImageBitmap};
    let closed = 0;
    const bitmap = {width: 3840, height: 2160, close: () => closed++};
    const canvas = {getContext: () => ({drawImage() {}}), toDataURL: () => 'data:image/webp;base64,AAAA'};
    globalThis.document = {createElement: () => canvas};
    globalThis.createImageBitmap = async () => bitmap;
    try {
        assert.ok(validLocalImage(await loadLocalImage({type: 'image/png', size: 100})));
        assert.deepEqual([canvas.width, canvas.height, closed], [1920, 1080, 1]);
        await assert.rejects(loadLocalImage({type: 'image/svg+xml', size: 100}), /PNG, JPEG or WebP/);
        await assert.rejects(loadLocalImage({type: 'image/png', size: 11 * 1024 * 1024}), /10 MiB/);
        canvas.toDataURL = () => { throw new Error('encode failed'); };
        await assert.rejects(loadLocalImage({type: 'image/png', size: 100}), /Could not prepare/);
        assert.equal(closed, 2);
    } finally {
        for (const [name, value] of Object.entries(originals)) {
            if (value === undefined) delete globalThis[name];
            else globalThis[name] = value;
        }
    }
});
