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
import semantic from '../../src/features/semantic-segmentation.js';
import {resolveOverlay} from '../../src/overlay-features.js';
import {indexFrame, outputSource, sourceKey} from '../../src/overlay-sources.js';
import {createCoordinateMapper} from '../../src/overlay-drawing.js';
import {createOverlay, loadOverlays, saveOverlays} from '../../src/overlays.js';

const video = {payloadType: 'FrameContext', detections: [{type: 'VideoFrame', data: {
    uuid: '42', originalWidth: 3, originalHeight: 2, sourceCropLeft: 0,
}}]};
function fixture(pixels = [0, 13, 18, 0, 13, 18], family = 'example-classes') {
    return {payloadType: 'SemanticSegmentationMasks', contentType: 'semanticSegmentation',
        model: 'semantic', labelFamily: family, detections: [{type: 'SemanticSegmentationMask', data: {
            width: 3, height: 2, valueType: 'Uint8', parentUuid: '42', pixels: Uint8Array.from(pixels),
        }}]};
}
function resolved(layer, frames = [video, layer]) {
    return resolveOverlay(indexFrame({layers: frames}), createOverlay(semantic.id, outputSource(layer)));
}

test('semantic sources are separate from alpha/instance masks and persist styles without frame data', () => {
    const layer = fixture();
    const catalog = new Map([layer, {...layer, model: 'other'}, {...layer, payloadType: 'SegmentationMasks'},
        {...layer, payloadType: 'InstanceSegmentationMasks'}, {...layer, contentType: 'segmentation'}]
        .map(item => [sourceKey(outputSource(item)), outputSource(item)]));
    assert.deepEqual(semantic.discover(catalog).map(source => source.model), ['semantic', 'other']);
    const overlay = createOverlay(semantic.id, outputSource(layer));
    assert.deepEqual(overlay.settings, {opacity: 0.5, classes: {}});
    assert.equal(resolved(layer, [video, {...layer, model: 'other'}]).state, 'waiting');
    let saved;
    const storage = {getItem: () => saved, setItem: (_key, value) => { saved = value; }};
    assert.equal(saveOverlays(storage, [overlay]), '');
    assert.deepEqual(loadOverlays(storage).overlays, [overlay]);
    assert.equal(saved.includes('pixels'), false);
    assert.equal(saved.includes('legend'), false);
});

test('classes come only from mask IDs or saved styles, independently of label family', () => {
    const first = semantic.legend(resolved(fixture()));
    assert.equal(first.entries.length, 3, 'no dataset-specific class inventory is invented');
    assert.deepEqual(first.entries.filter(entry => entry.present).map(({id, label, color}) => ({id, label, color})), [
        {id: 0, label: 'Class 0', color: '#404040'},
        {id: 13, label: 'Class 13', color: '#b85840'},
        {id: 18, label: 'Class 18', color: '#707040'},
    ]);
    assert.equal(first.message, '');
    assert.deepEqual(semantic.legend(resolved(fixture([18, 0, 13, 18, 0, 13]))), first);
    const unknown = semantic.legend(resolved(fixture([0, 255, 19, 0, 255, 19])));
    assert.deepEqual(unknown.entries.filter(entry => entry.present).map(entry => entry.label), ['Class 0', 'Class 19', 'Class 255']);
    assert.equal(unknown.message, '');
    for (const family of ['cityscapes', 'unknown-family', '']) {
        assert.deepEqual(semantic.legend(resolved(fixture([0, 13, 18, 0, 13, 18], family))), first);
    }
    const layer = fixture();
    assert.deepEqual(semantic.legend(resolved(layer)), semantic.legend(resolved(layer)));
});

test('invalid, ambiguous and cropped maps cannot render or supply legend entries', () => {
    const layer = fixture();
    for (const change of [{width: 0}, {height: -1}, {width: 8193}, {width: 8192, height: 8192},
        {valueType: 'Uint32'}, {pixels: [0, 1, 2, 3, 4, 5]}, {pixels: new Uint8Array(5)},
        {width: 2, height: 3}, {parentUuid: '0'}, {parentUuid: 'missing'}]) {
        const bad = {...layer, detections: [{type: 'SemanticSegmentationMask', data: {...layer.detections[0].data, ...change}}]};
        const result = resolved(bad);
        assert.notEqual(result.state, 'ready');
        assert.equal(result.mask, undefined);
        assert.ok(semantic.legend(result).entries.every(entry => !entry.present));
    }
    assert.equal(resolved(layer, [video, video, layer]).state, 'partial');
    assert.equal(resolved(layer, [video, layer, layer]).state, 'ambiguous');
    assert.equal(resolved(layer, [layer]).state, 'partial');
    const crop = {...video, detections: [{type: 'VideoFrame', data: {...video.detections[0].data, sourceCropLeft: 1}}]};
    assert.equal(resolved(layer, [crop, layer]).state, 'unsupported');
    const empty = resolved({...layer, detections: []});
    assert.equal(empty.state, 'ready');
    assert.ok(semantic.legend(empty).entries.every(entry => !entry.present));
});

test('colorization preserves opaque class bytes and reuses its surface across redraws', () => {
    const originals = {document: globalThis.document, ImageData: globalThis.ImageData};
    const uploads = [];
    globalThis.document = {createElement: () => ({getContext: () => ({putImageData: image => uploads.push(image)})})};
    globalThis.ImageData = class { constructor(data, width, height) { Object.assign(this, {data, width, height}); } };
    try {
        const layer = fixture();
        const result = resolved(layer);
        const overlay = createOverlay(semantic.id, outputSource(layer));
        const calls = [];
        const ctx = {drawImage: (...args) => calls.push(args), imageSmoothingEnabled: true};
        const mapper = createCoordinateMapper({canvasWidth: 600, canvasHeight: 600, videoWidth: 300, videoHeight: 200});
        const target = {ctx, mapper, overlay};
        semantic.render(target, result);
        semantic.render(target, result);
        assert.equal(uploads.length, 1);
        assert.equal(ctx.imageSmoothingEnabled, false);
        assert.deepEqual(calls[0].slice(1), [0, 100, 600, 400]);
        assert.deepEqual([...uploads[0].data.slice(0, 12)], [64, 64, 64, 255, 184, 88, 64, 255, 112, 112, 64, 255]);
        semantic.render(target, resolved(fixture([255, 0, 18, 255, 0, 18])));
        assert.equal(uploads.length, 2);
        assert.equal(uploads[1].data[3], 255);

        const settings = {opacity: 0.5, classes: {
            0: {enabled: true, color: '#00ff00', opacity: 0.5},
            13: {enabled: false, color: '#ffffff', opacity: 1},
        }};
        semantic.render(target, result, settings);
        assert.deepEqual([...uploads.at(-1).data.slice(0, 12)], [0, 255, 0, 128, 255, 255, 255, 0, 112, 112, 64, 255]);
        const otherTarget = {...target, overlay: createOverlay(semantic.id, outputSource(layer))};
        semantic.render(otherTarget, result);
        assert.deepEqual([...uploads.at(-1).data.slice(0, 4)], [64, 64, 64, 255]);
        const beforeRedraw = uploads.length;
        semantic.render(target, result, settings);
        semantic.render(otherTarget, result);
        assert.equal(uploads.length, beforeRedraw, 'different overlay styles keep independent caches');
        settings.classes[0].label = 'person';
        semantic.render(target, result, settings);
        assert.equal(uploads.length, beforeRedraw, 'renaming a class does not recolor the mask');
        settings.classes[0].opacity = 0;
        semantic.render(target, result, settings);
        assert.equal(uploads.at(-1).data[3], 0, 'style changes invalidate the paused-frame cache');
    } finally {
        for (const [key, value] of Object.entries(originals)) {
            if (value === undefined) delete globalThis[key]; else globalThis[key] = value;
        }
    }
});

test('class preferences round trip, migrate opacity-only settings and reject malformed saved styles', () => {
    assert.deepEqual(semantic.validateSettings({opacity: 0.3}), {opacity: 0.3, classes: {}});
    const overlay = createOverlay(semantic.id, outputSource(fixture()));
    overlay.settings.classes = {0: {enabled: false, color: '#123456', opacity: 0.4}};
    let saved;
    const storage = {getItem: () => saved, setItem: (_key, value) => { saved = value; }};
    assert.equal(saveOverlays(storage, [overlay]), '');
    assert.deepEqual(loadOverlays(storage).overlays, [overlay]);
    assert.deepEqual(createOverlay(semantic.id).settings.classes, {}, 'new overlays do not share preferences');
    for (const classes of [null, [], 'invalid', {'256': {enabled: true, color: '#ffffff', opacity: 1}},
        {'01': {enabled: true, color: '#ffffff', opacity: 1}},
        {0: {enabled: 'false', color: '#ffffff', opacity: 1}},
        {0: {enabled: true, color: 'red', opacity: 1}},
        {0: {enabled: true, color: '#ffffff', opacity: 1.1}}]) {
        assert.equal(semantic.validateSettings({opacity: 0.5, classes}), null);
    }
    const legend = semantic.legend(resolved(fixture()), overlay.settings, outputSource(fixture()));
    assert.deepEqual(legend.entries[0], {id: 0, label: 'Class 0', color: '#123456', opacity: 0.4, enabled: false, present: true});
    assert.deepEqual(semantic.legend({}, overlay.settings).entries, [{...legend.entries[0], present: false}]);
    overlay.settings.classes[0].label = 'person';
    assert.equal(saveOverlays(storage, [overlay]), '');
    assert.deepEqual(loadOverlays(storage).overlays, [overlay]);
    assert.equal(semantic.legend({}, overlay.settings).entries[0].label, 'person');
    assert.equal(semantic.legend(resolved(fixture())).entries[0].label, 'Class 0', 'names are local to each overlay');
    for (const label of [null, 7, 'x'.repeat(81)]) {
        assert.equal(semantic.validateSettings({opacity: 0.5, classes: {0: {...overlay.settings.classes[0], label}}}), null);
    }
    const cleared = semantic.validateSettings({opacity: 0.5, classes: {0: {...overlay.settings.classes[0], label: '  '}}});
    assert.equal(cleared.classes[0].label, undefined);
    assert.equal(semantic.legend({}, cleared).entries[0].label, 'Class 0');
    const trimmed = semantic.validateSettings({opacity: 0.5, classes: {0: {...overlay.settings.classes[0], label: ' person '}}});
    assert.equal(trimmed.classes[0].label, 'person');
});
