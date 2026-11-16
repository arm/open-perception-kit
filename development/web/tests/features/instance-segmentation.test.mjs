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
import {BitmapEncoding} from '../../src/mask-data.js';
import {CoordinateUnits} from '../../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/coordinate-units.js';

const space = (width = 7, height = 5) => ({frameId: 42n, width, height, originX: 0, originY: 0, extentWidth: width, extentHeight: height});

const video = {payloadType: 'FrameContext', detections: [{type: 'VideoFrame', data: {
    uuid: '42', originalWidth: 7, originalHeight: 5,
}}]};
function mask(x = 1, width = 4, classId = 0) {
    const pixels = new Uint8Array(35);
    for (let y = 1; y < 4; y++) pixels.fill(1, y * 7 + x, y * 7 + x + width);
    return {type: 'InstanceSegmentationMask', data: {parentUuid: '42', width: 7, height: 5,
        encoding: BitmapEncoding.Uint8, pixels, classId, labelSet: 'test:classes', confidence: 0.75, space: space(),
        box: {x, y: 1, width, height: 3, space: space(), units: CoordinateUnits.Pixels}}};
}
function layer(detections = [mask(), mask(3, 3)]) {
    return {payloadType: 'InstanceSegmentationMasks', contentType: 'instanceSegmentation',
        model: 'instance', labelFamily: 'arbitrary', detections};
}
function resolve(output, frames = [video, output]) {
    return resolveOverlay(indexFrame({layers: frames}), createOverlay(instance.id, outputSource(output)));
}

test('instance sources and class settings distinguish absent and wide unsigned IDs', () => {
    const output = layer([mask(1, 4, 500), mask(3, 3, null)]);
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
    assert.deepEqual(legend.entries.map(entry => [entry.id, entry.label]), [[-1, 'Unclassified'], [500, 'person']]);
    assert.ok(semantic.validateSettings(overlay.settings), 'semantic maps can also carry wide IDs');
    for (const id of [-2, 4294967296]) {
        assert.equal(instance.validateSettings({...overlay.settings, classes: {[id]: overlay.settings.classes[500]}}), null);
    }
    assert.deepEqual(createOverlay(instance.id, outputSource(other)).settings.classes, {});
});

test('invalid instance metadata fails closed; empty masks do not report present classes', () => {
    for (const change of [{width: 0}, {width: 8193}, {encoding: 0}, {pixels: new Uint8Array(34)},
        {pixels: new Uint8Array(35).fill(255)}, {space: null}, {classId: -1}, {classId: 0.5},
        {confidence: NaN}, {confidence: 1.1}, {labelSet: ''}, {box: {x: 0, y: 0, width: 8, height: 1}}]) {
        const bad = mask();
        Object.assign(bad.data, change);
        const result = resolve(layer([mask(), bad]));
        assert.notEqual(result.state, 'ready');
        assert.deepEqual(result.items, [], 'a malformed sibling cannot leave a partially misleading raster');
    }
    const output = layer();
    const rounded = mask();
    rounded.data.box = {...rounded.data.box, x: Math.fround(1.1), y: 0, width: Math.fround(5.9), height: 5};
    assert.equal(resolve(layer([rounded])).state, 'ready', 'wire Float32 rounding at the frame edge is tolerated');
    assert.equal(resolve(output, [video, video, output]).state, 'ready');
    assert.equal(resolve(output, [output]).state, 'ready');
    const otherVideo = {...video, detections: [{type: 'VideoFrame', data: {...video.detections[0].data, uuid: '43'}}]};
    const second = mask();
    second.data.space = {...space(), frameId: 43n};
    second.data.box = null;
    const mixed = layer([mask(), second]);
    assert.equal(resolve(mixed, [video, otherVideo, mixed]).state, 'ambiguous');
    assert.equal(resolve(layer(Array.from({length: 513}, () => mask()))).state, 'unsupported');
    const empty = mask();
    empty.data.pixels.fill(0);
    assert.deepEqual(resolve(layer([empty])).items, []);
    const outside = mask();
    outside.data.pixels[0] = 255;
    assert.equal(resolve(layer([outside])).state, 'unsupported', 'binary validation also covers pixels outside the box');
    outside.data.pixels[0] = 1;
    assert.deepEqual(resolve(layer([outside])).items[0].bounds, {left: 0, right: 4, top: 0, bottom: 3});
    assert.deepEqual(instance.legend(resolve(layer([]))).entries, []);

    const large = mask();
    Object.assign(large.data, {width: 512, height: 512, pixels: new Uint8Array(512 * 512)});
    const largeVideo = {...video, detections: [{type: 'VideoFrame', data: {...video.detections[0].data, originalWidth: 512, originalHeight: 512}}]};
    const atLimit = layer(Array(256).fill(large));
    assert.equal(resolve(atLimit, [largeVideo, atLimit]).state, 'ready');
    const largeOutput = layer(Array(257).fill(large));
    assert.match(resolve(largeOutput, [largeVideo, largeOutput]).message, /pixel budget/);
});

test('aggregate instance budget supports nine full-HD and three UHD masks', () => {
    for (const [width, height, count] of [[1920, 1080, 9], [3840, 2160, 3]]) {
        // Reuse storage in the fixture; the budget must still count every declared mask.
        const pixels = new Uint8Array(width * height);
        pixels[0] = 1;
        const output = layer(Array.from({length: count}, () => ({type: 'InstanceSegmentationMask', data: {
            ...mask().data, width, height, pixels, space: space(width, height), box: null,
        }})));
        const parent = {...video, detections: [{type: 'VideoFrame', data: {
            ...video.detections[0].data, originalWidth: width, originalHeight: height,
        }}]};
        const result = resolve(output, [parent, output]);
        assert.equal(result.state, 'ready');
        assert.equal(result.items.length, count);
    }
});

test('optional annotations and independently sized crop grids retain declared frame identity', () => {
    const crop = mask();
    Object.assign(crop.data, {width: 2, height: 1, pixels: new Uint8Array([1, 0]),
        space: {...space(), originX: 2, originY: 1, extentWidth: 3, extentHeight: 2},
        classId: null, confidence: null, box: null, labelSet: ''});
    const output = layer([mask(1, 4, 0xffffffff), crop]);
    const result = resolve(output, [output]);
    assert.equal(result.state, 'ready');
    assert.equal(result.items.length, 2);
    assert.deepEqual(instance.legend(result).entries.map(item => [item.id, item.label]), [[-1, 'Unclassified'], [0xffffffff, 'Class 4294967295']]);
    const mismatched = mask();
    mismatched.data.labelSet = 'other:classes';
    assert.equal(resolve(layer([mask(), mismatched])).state, 'ambiguous');
    const noAnnotation = resolve(layer([crop]));
    instance.renderAnnotations({ctx: {save() { assert.fail('missing boxes must not become fabricated annotations'); }}}, noAnnotation, noAnnotation.settings);
});

test('binary foreground, outlines, overlap order, class alpha and frame-local annotations render correctly', () => {
    const originals = {document: globalThis.document, ImageData: globalThis.ImageData};
    const uploads = [];
    const operations = [];
    globalThis.document = {createElement: () => {
        const context = {putImageData: image => uploads.push(image), clearRect() {},
            drawImage(...args) { operations.push({mode: this.globalCompositeOperation, placement: args.slice(1)}); }};
        return {getContext: () => context};
    }};
    globalThis.ImageData = class { constructor(data, width, height) { Object.assign(this, {data, width, height}); } };
    try {
        const output = layer();
        const index = indexFrame({layers: [video, output]});
        const overlay = createOverlay(instance.id, outputSource(output));
        overlay.settings.boxes = false;
        const result = resolveOverlay(index, overlay);
        assert.equal(resolveOverlay(index, overlay).items, result.items, 'validation reused across animation redraws');
        const draws = [];
        const labels = [];
        const stack = [];
        const ctx = {globalAlpha: 0.5, drawImage: (...args) => draws.push(args), getTransform: () => ({a: 1}),
            save() { stack.push(this.globalAlpha); }, restore() { this.globalAlpha = stack.pop(); },
            measureText: text => ({width: text.length * 7}), fillRect() {}, strokeRect() {},
            fillText(text) { labels.push({text, alpha: this.globalAlpha}); }};
        const mapper = createCoordinateMapper({canvasWidth: 700, canvasHeight: 700, videoWidth: 7, videoHeight: 5});
        const target = {ctx, mapper, overlay};
        const pixel = (x, y, image = uploads.at(-1)) => [...image.data.slice((y * 7 + x) * 4, (y * 7 + x) * 4 + 4)];
        instance.render(target, result);
        assert.deepEqual(draws[0].slice(1).map(Math.round), [0, 100, 700, 500]);
        assert.equal(ctx.imageSmoothingEnabled, false);
        assert.deepEqual(pixel(2, 2, uploads[1]), [64, 64, 64, 255], 'byte 1 means foreground, not 1/255 alpha');
        assert.deepEqual(pixel(3, 2), [255, 255, 255, 255], 'later instance outline separates same-class objects');
        assert.deepEqual(pixel(4, 2), [64, 64, 64, 255]);
        assert.deepEqual(pixel(2, 1, uploads[1]), [255, 255, 255, 255], 'top edge uses the upper neighbor');
        assert.deepEqual(pixel(2, 3, uploads[1]), [255, 255, 255, 255], 'bottom edge uses the lower neighbor');
        assert.deepEqual(pixel(0, 0), [0, 0, 0, 0]);
        instance.render(target, result);
        assert.equal(uploads.length, 4);
        assert.deepEqual(operations.map(item => item.mode), ['destination-out', 'source-over', 'destination-out', 'source-over']);
        assert.ok(operations.every(item => item.placement.join() === '0,0,700,500'));

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
        assert.deepEqual(labels, [], 'raster pass does not draw annotations');
        instance.renderAnnotations(target, changedResult, settings);
        assert.deepEqual(labels.map(item => item.text), ['#1 · Class 0 · 75%', '#2 · person · 75%']);
        assert.deepEqual(labels.map(item => item.alpha), [0.2, 0.4]);
        const beforeRename = uploads.length;
        settings.classes[500].label = 'renamed';
        instance.render(target, changedResult, settings);
        instance.renderAnnotations(target, changedResult, settings);
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

test('composition follows display size and DPR, invalidates cached frames and stays bounded', () => {
    const originals = {document: globalThis.document, ImageData: globalThis.ImageData};
    const canvases = [];
    let uploads = 0;
    globalThis.document = {createElement: () => {
        const context = {putImageData() { uploads++; }, clearRect() {}, drawImage() {}};
        const canvas = {width: 0, height: 0, getContext: () => context};
        canvases.push(canvas);
        return canvas;
    }};
    globalThis.ImageData = class {};
    try {
        const output = layer();
        const result = resolve(output);
        const target = {ctx: {drawImage() {}, dpr: 1, getTransform() { return {a: this.dpr}; }},
            overlay: createOverlay(instance.id, outputSource(output)), mapper: {display: {x: 0, y: 100, width: 700, height: 500}}};
        instance.render(target, result);
        assert.deepEqual([canvases[0].width, canvases[0].height], [700, 500]);
        const initialUploads = uploads;
        instance.render(target, result);
        assert.equal(uploads, initialUploads);
        target.ctx.dpr = 2;
        instance.render(target, result);
        assert.deepEqual([canvases[0].width, canvases[0].height], [1400, 1000]);
        assert.equal(uploads, initialUploads * 2, 'DPR change repaints the paused frame');
        Object.assign(target.mapper.display, {width: 350, height: 250});
        instance.render(target, result);
        assert.deepEqual([canvases[0].width, canvases[0].height], [700, 500]);
        assert.equal(uploads, initialUploads * 3, 'display resize repaints the paused frame');
        target.ctx.dpr = 1.5;
        Object.assign(target.mapper.display, {width: 1052 / 1.5, height: 750 / 1.5});
        instance.render(target, result);
        assert.deepEqual([canvases[0].width, canvases[0].height], [1052, 750], 'use the render transform, not a rounded backing/CSS ratio');
        target.ctx.dpr = 2;
        Object.assign(target.mapper.display, {width: 10000, height: 10000});
        instance.render(target, result);
        assert.ok(canvases[0].width <= 8192 && canvases[0].height <= 8192);
        assert.ok(canvases[0].width * canvases[0].height <= 16_777_216);
        const beforeInvalid = uploads;
        target.mapper.display.width = 0;
        instance.render(target, result);
        assert.equal(uploads, beforeInvalid);
    } finally {
        for (const [key, value] of Object.entries(originals)) {
            if (value === undefined) delete globalThis[key]; else globalThis[key] = value;
        }
    }
});
