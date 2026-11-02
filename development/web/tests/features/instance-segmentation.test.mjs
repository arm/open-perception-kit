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
import instance from '../../src/features/instance-segmentation.js';
import semantic from '../../src/features/semantic-segmentation.js';
import {resolveOverlay} from '../../src/overlay-features.js';
import {indexFrame, outputSource, sourceKey} from '../../src/overlay-sources.js';
import {createOverlay, loadOverlays, saveOverlays} from '../../src/overlays.js';
import {createCoordinateMapper} from '../../src/overlay-drawing.js';

const video = {payloadType: 'FrameContext', detections: [{type: 'VideoFrame', data: {
    uuid: '42', originalWidth: 7, originalHeight: 5,
}}]};
function mask(x = 1, width = 4, classId = 0) {
    const pixels = new Uint8Array(35);
    for (let y = 1; y < 4; y++) pixels.fill(1, y * 7 + x, y * 7 + x + width);
    return {type: 'InstanceSegmentationMask', data: {parentUuid: '42', width: 7, height: 5,
        valueType: 'Uint8', pixels, classId, confidence: 0.75, box: {x, y: 1, width, height: 3}}};
}
function layer(detections = [mask(), mask(3, 3)]) {
    return {payloadType: 'InstanceSegmentationMasks', contentType: 'instanceSegmentation',
        model: 'instance', labelFamily: 'arbitrary', detections};
}
function resolve(output, frames = [video, output]) {
    return resolveOverlay(indexFrame({layers: frames}), createOverlay(instance.id, outputSource(output)));
}

test('instance sources and class settings stay independent, including signed/wide class IDs', () => {
    const output = layer([mask(1, 4, 500), mask(3, 3, -1)]);
    const other = {...output, model: 'other'};
    const catalog = new Map([output, other, {...output, payloadType: 'SemanticSegmentationMasks'},
        {...output, payloadType: 'SegmentationMasks'}].map(item => [sourceKey(outputSource(item)), outputSource(item)]));
    assert.deepEqual(instance.discover(catalog).map(source => source.model), ['instance', 'other']);
    assert.equal(resolve(output, [video, other]).state, 'waiting');
    const overlay = createOverlay(instance.id, outputSource(output));
    assert.deepEqual(overlay.settings, {opacity: 0.5, boxes: true, classes: {}});
    overlay.settings.classes[500] = {enabled: false, color: '#123456', opacity: 0.4, label: 'person'};
    let saved;
    const storage = {getItem: () => saved, setItem: (_key, value) => { saved = value; }};
    assert.equal(saveOverlays(storage, [overlay]), '');
    assert.deepEqual(loadOverlays(storage).overlays, [overlay]);
    assert.equal(saved.includes('pixels'), false);
    const legend = instance.legend(resolve(output), overlay.settings);
    assert.deepEqual(legend.entries.map(entry => [entry.id, entry.label]), [[-1, 'Class -1'], [500, 'person']]);
    assert.equal(semantic.validateSettings(overlay.settings), null, 'semantic IDs remain Uint8-only');
    for (const id of [-2, 2147483648]) {
        assert.equal(instance.validateSettings({...overlay.settings, classes: {[id]: overlay.settings.classes[500]}}), null);
    }
    assert.deepEqual(createOverlay(instance.id, outputSource(other)).settings.classes, {});
});

test('invalid instance metadata fails closed; empty masks do not report present classes', () => {
    for (const change of [{width: 0}, {width: 8193}, {valueType: 'Float32'}, {pixels: new Uint8Array(34)},
        {pixels: new Uint8Array(35).fill(255)}, {parentUuid: 'missing'}, {classId: -2}, {classId: 0.5},
        {confidence: NaN}, {confidence: 1.1}, {box: null}, {box: {x: 0, y: 0, width: 8, height: 1}}]) {
        const bad = mask();
        Object.assign(bad.data, change);
        const result = resolve(layer([mask(), bad]));
        assert.notEqual(result.state, 'ready');
        assert.deepEqual(result.masks, [], 'a malformed sibling cannot leave a partially misleading raster');
    }
    const output = layer();
    const rounded = mask();
    rounded.data.box = {x: Math.fround(1.1), y: 0, width: Math.fround(5.9), height: 5};
    assert.equal(resolve(layer([rounded])).state, 'ready', 'wire Float32 rounding at the frame edge is tolerated');
    assert.equal(resolve(output, [video, video, output]).state, 'partial');
    const cropped = {...video, detections: [{type: 'VideoFrame', data: {...video.detections[0].data, sourceCropLeft: 1}}]};
    assert.equal(resolve(output, [cropped, output]).state, 'unsupported');
    const otherVideo = {...video, detections: [{type: 'VideoFrame', data: {...video.detections[0].data, uuid: '43'}}]};
    const second = mask();
    second.data.parentUuid = '43';
    const mixed = layer([mask(), second]);
    assert.equal(resolve(mixed, [video, otherVideo, mixed]).state, 'ambiguous');
    assert.equal(resolve(layer(Array.from({length: 513}, () => mask()))).state, 'unsupported');
    const empty = mask();
    empty.data.pixels.fill(0);
    assert.deepEqual(resolve(layer([empty])).items, []);
    assert.deepEqual(instance.legend(resolve(layer([]))).entries, []);

    const large = mask();
    Object.assign(large.data, {width: 512, height: 512, pixels: new Uint8Array(512 * 512)});
    const largeVideo = {...video, detections: [{type: 'VideoFrame', data: {...video.detections[0].data, originalWidth: 512, originalHeight: 512}}]};
    const largeOutput = layer(Array(65).fill(large));
    assert.match(resolve(largeOutput, [largeVideo, largeOutput]).message, /pixel budget/);
});

test('binary foreground, outlines, overlap order, class alpha and frame-local annotations render correctly', () => {
    const originals = {document: globalThis.document, ImageData: globalThis.ImageData};
    const uploads = [];
    globalThis.document = {createElement: () => ({getContext: () => ({putImageData: image => uploads.push(image)})})};
    globalThis.ImageData = class { constructor(data, width, height) { Object.assign(this, {data, width, height}); } };
    try {
        const output = layer();
        const index = indexFrame({layers: [video, output]});
        const overlay = createOverlay(instance.id, outputSource(output));
        overlay.settings.boxes = false;
        const result = resolveOverlay(index, overlay);
        assert.equal(resolveOverlay(index, overlay).masks, result.masks, 'validation reused across animation redraws');
        const draws = [];
        const labels = [];
        const stack = [];
        const ctx = {globalAlpha: 0.5, drawImage: (...args) => draws.push(args),
            save() { stack.push(this.globalAlpha); }, restore() { this.globalAlpha = stack.pop(); },
            measureText: text => ({width: text.length * 7}), fillRect() {}, strokeRect() {},
            fillText(text) { labels.push({text, alpha: this.globalAlpha}); }};
        const mapper = createCoordinateMapper({canvasWidth: 700, canvasHeight: 700, videoWidth: 7, videoHeight: 5});
        const target = {ctx, mapper, overlay};
        const pixel = (x, y) => [...uploads.at(-1).data.slice((y * 7 + x) * 4, (y * 7 + x) * 4 + 4)];
        instance.render(target, result);
        assert.deepEqual(draws[0].slice(1).map(Math.round), [0, 100, 700, 500]);
        assert.equal(ctx.imageSmoothingEnabled, false);
        assert.deepEqual(pixel(2, 2), [64, 64, 64, 255], 'byte 1 means foreground, not 1/255 alpha');
        assert.deepEqual(pixel(3, 2), [255, 255, 255, 255], 'later instance outline separates same-class objects');
        assert.deepEqual(pixel(4, 2), [64, 64, 64, 255]);
        assert.deepEqual(pixel(0, 0), [0, 0, 0, 0]);
        instance.render(target, result);
        assert.equal(uploads.length, 1);

        const changed = layer([mask(), mask(3, 3, 500)]);
        const changedResult = resolve(changed);
        const settings = {boxes: false, classes: {
            0: {enabled: true, color: '#0000ff', opacity: 0.4},
            500: {enabled: true, color: '#00ff00', opacity: 0.8, label: 'person'},
        }};
        instance.render(target, changedResult, settings);
        assert.deepEqual(pixel(4, 2), [0, 255, 0, 204], 'later foreground wins without accumulating alpha');
        settings.classes[500].enabled = false;
        instance.render(target, changedResult, settings);
        assert.deepEqual(pixel(4, 2), [255, 255, 255, 102], 'hiding the top instance reveals the lower mask');
        settings.classes[500].enabled = true;
        settings.boxes = true;
        instance.render(target, changedResult, settings);
        assert.deepEqual(labels.map(item => item.text), ['#1 · Class 0 · 75%', '#2 · person · 75%']);
        assert.deepEqual(labels.map(item => item.alpha), [0.2, 0.4]);
        const beforeRename = uploads.length;
        settings.classes[500].label = 'renamed';
        instance.render(target, changedResult, settings);
        assert.equal(uploads.length, beforeRename);
        assert.equal(labels.at(-1).text, '#2 · renamed · 75%');
        instance.render(target, resolve(layer([])), settings);
        assert.equal(uploads.length, beforeRename);
    } finally {
        for (const [key, value] of Object.entries(originals)) {
            if (value === undefined) delete globalThis[key]; else globalThis[key] = value;
        }
    }
});
