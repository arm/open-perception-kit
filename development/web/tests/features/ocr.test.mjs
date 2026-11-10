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
import ocr from '../../src/features/ocr.js';
import {resolveOverlay, overlayStatus} from '../../src/overlay-features.js';
import {indexFrame, outputSource, sourceKey} from '../../src/overlay-sources.js';
import {bindPendingOverlays, createOverlay, loadOverlays, saveOverlays} from '../../src/overlays.js';
import {render, select} from '../overlay-test-utils.mjs';

const video = {payloadType: 'FrameContext', detections: [{type: 'VideoFrame', data: {
    uuid: '42', originalWidth: 640, originalHeight: 480,
}}]};
function fixture() {
    const detector = {payloadType: 'BoxDetections', model: 'regions', contentType: 'arbitrary',
        detections: [
            {type: 'Rect', data: {uuid: '9007199254740993', parentUuid: '42', x: 80, y: 0, width: 160, height: 40, confidence: 0.8}},
            {type: 'Rect', data: {uuid: '201', parentUuid: '42', x: 300, y: 200, width: 180, height: 32, confidence: 0.7}},
        ]};
    const reader = {payloadType: 'Classifications', model: 'reader', contentType: '',
        producer: {instanceId: 'another-producer', component: 'parser'}, detections: [
            {type: 'Classification', data: {uuid: '301', parentUuid: '201', candidates: [{text: '東京 AB', confidence: 0.9}]}},
            {type: 'Classification', data: {uuid: '302', parentUuid: '9007199254740993', candidates: [{text: 'PLATE 1234', confidence: 0.95}]}},
        ]};
    return {detector, reader, frame: {layers: [video, detector, reader]}};
}
function resolved(frame, source) { return resolveOverlay(indexFrame(frame), createOverlay('ocr', outputSource(source))); }

test('OCR requires an explicit compatible source, without content-type or model-name inference', () => {
    const {detector, reader, frame} = fixture();
    const source = outputSource(reader);
    const catalog = new Map([[sourceKey(source), source]]);
    const pending = createOverlay('ocr');
    assert.equal(bindPendingOverlays([pending], catalog), false);
    assert.equal(pending.selection.primary, null);
    assert.equal(overlayStatus(indexFrame(frame), pending, catalog), 'Choose a source');
    assert.equal(overlayStatus(indexFrame(null), pending, new Map()), 'Waiting for source');
    assert.equal(overlayStatus(indexFrame(null), {featureId: 'unknown', selection: {primary: null}}, catalog), 'Unsupported · Unknown rendering feature.');
    assert.equal(ocr.discover(catalog).length, 1);
    for (const contentType of ['', 'textRecognition', 'cameraContact', 'humanFace']) {
        assert.equal(ocr.discover(new Map([['source', {...source, contentType}]])).length, 1);
        assert.equal(ocr.defaults({...source, contentType}).color, '#00ffff');
    }
    assert.equal(ocr.discover(new Map([['mask', {...source, payloadType: 'SemanticSegmentationMasks'}]])).length, 0);
    const overlay = select('ocr', reader, {textSize: '24', regions: false, opacity: 0.4});
    let saved;
    const storage = {getItem: () => saved, setItem: (_key, value) => { saved = value; }};
    assert.equal(saveOverlays(storage, [overlay]), '');
    assert.deepEqual(loadOverlays(storage).overlays, [overlay]);
    assert.equal(saved.includes('PLATE 1234'), false);
    assert.equal(ocr.validateSettings({...overlay.settings, textSize: '1000'}), null);
    assert.equal(resolved({layers: [video, detector, {...reader, model: 'other'}]}, reader).state, 'waiting');
});

test('recognition joins full-frame parent boxes by ID, not by order or producer name', () => {
    const {detector, reader, frame} = fixture();
    const other = {...reader, model: 'other reader', detections: [{...reader.detections[0], data: {
        ...reader.detections[0].data, candidates: [{text: 'WRONG', confidence: 1}],
    }}]};
    frame.layers.unshift(other);
    const index = indexFrame(frame);
    const overlay = select('ocr', reader);
    const result = resolveOverlay(index, overlay);
    assert.equal(result.state, 'ready');
    assert.deepEqual(result.regions.map(region => [region.text, region.box.x]), [['東京 AB', 300], ['PLATE 1234', 80]]);
    assert.equal(resolveOverlay(index, overlay).regions, result.regions);
    assert.equal(resolved(frame, detector).regions.every(region => region.text === ''), true, 'detector view never borrows a recognizer');
    reader.detections[0].data.candidates = [{text: '', confidence: 0}];
    assert.equal(resolved(frame, reader).regions[0].text, '', 'empty recognition is a valid unlabelled region');
    reader.detections[0].data.candidates[0].text = ' \n'.repeat(600);
    assert.equal(resolved(frame, reader).regions[0].text, '', 'whitespace-only results never become an ellipsis label');
    reader.detections = [];
    assert.deepEqual(resolved(frame, reader).items, []);
});

test('missing, ambiguous and malformed OCR relationships do not invent text or geometry', () => {
    const {detector, reader, frame} = fixture();
    assert.equal(resolved({layers: [reader]}, reader).state, 'partial');
    assert.deepEqual(resolved({layers: [reader]}, reader).regions, []);
    const duplicate = {...detector, detections: [detector.detections[0]]};
    const ambiguous = resolved({layers: [...frame.layers, duplicate]}, reader);
    assert.equal(ambiguous.state, 'ambiguous');
    assert.deepEqual(ambiguous.regions.map(region => region.text), ['東京 AB']);
    const repeated = {...reader, detections: [reader.detections[0], reader.detections[0]]};
    assert.equal(resolved({layers: [video, detector, repeated]}, repeated).regions[0].text, '');
    const crop = {...video, detections: [{type: 'VideoFrame', data: {...video.detections[0].data, sourceCropTop: 1}}]};
    assert.equal(resolved({layers: [crop, detector, reader]}, reader).state, 'unsupported');
    assert.equal(resolved({layers: [video, video, detector, reader]}, reader).state, 'ambiguous');
    for (const change of [{x: NaN}, {y: Infinity}, {width: -1}, {width: 1000}, {confidence: 1.5}]) {
        const bad = {...detector, detections: [{type: 'Rect', data: {...detector.detections[0].data, ...change}}]};
        assert.equal(resolved({layers: [video, bad]}, bad).state, 'unsupported');
        assert.deepEqual(resolved({layers: [video, bad]}, bad).regions, []);
    }
    reader.detections[0].data.candidates.push({text: 'alternative', confidence: 0.1});
    const alternatives = resolved(frame, reader);
    assert.equal(alternatives.state, 'ambiguous');
    assert.equal(alternatives.regions[0].text, '');
    reader.detections[0].data.candidates = [{text: 'invalid score', confidence: NaN}];
    assert.equal(resolved(frame, reader).state, 'partial');
    assert.equal(resolved(frame, reader).regions[0].text, '');
    const huge = {...reader, detections: Array(4097).fill(reader.detections[0])};
    assert.equal(resolved({layers: [video, detector, huge]}, huge).state, 'unsupported');
});

test('OCR renders source-aligned outlines and bounded Unicode labels with independent controls', () => {
    const {detector, reader, frame} = fixture();
    const overlay = select('ocr', reader, {opacity: 0.5});
    const draw = () => render(frame, [overlay], 800, 800, 640, 480);
    const calls = draw().calls;
    assert.ok(calls.some(call => call.rect?.join() === '100,100,200,50'));
    assert.ok(calls.some(call => call.text === 'PLATE 1234' && call.y > 150), 'top-edge label goes below its region');
    assert.ok(calls.filter(call => call.text).every(call => call.x >= 0 && call.y >= 100 && call.y < 700));
    assert.ok(calls.filter(call => call.rect).every(call => call.alpha === 0.5));
    overlay.settings.regions = false;
    assert.equal(draw().calls.filter(call => call.rect).length, 0);
    overlay.settings.text = false;
    assert.equal(draw().calls.filter(call => call.text).length, 0);
    overlay.settings.textConfidence = true;
    assert.deepEqual(draw().calls.filter(call => call.text).map(call => call.text), ['Text: 90.0%', 'Text: 95.0%']);
    overlay.settings.regionConfidence = true;
    assert.deepEqual(draw().calls.filter(call => call.text).map(call => call.text),
        ['Region: 70.0% · Text: 90.0%', 'Region: 80.0% · Text: 95.0%']);
    overlay.settings.text = true;
    overlay.settings.textConfidence = false;
    overlay.settings.regionConfidence = false;
    const originalText = '<img src=x onerror=alert(1)> 東京 ' + '😀'.repeat(600);
    reader.detections[0].data.candidates[0].text = originalText;
    const text = draw().calls.find(call => call.text?.startsWith('<img')).text;
    assert.ok(text.endsWith('…'));
    assert.equal(/[\uD800-\uDFFF]/u.test(text), false, 'no split surrogate pairs');
    assert.equal(reader.detections[0].data.candidates[0].text, originalText, 'full text remains available for inspection/copy');
    overlay.settings.textConfidence = true;
    overlay.settings.regionConfidence = true;
    const withScores = draw();
    assert.ok(withScores.calls.find(call => call.text?.startsWith('<img')).text.endsWith('Region: 70.0% · Text: 90.0%'));
    assert.ok(withScores.calls.filter(call => call.text).every(call => withScores.measureText(call.text).width + 8 <= 800),
        'the entire label, including both scores and padding, fits the displayed width');
    const detections = render(frame, [select('ocr', detector, {regionConfidence: true, textConfidence: true})], 800, 800, 640, 480);
    assert.deepEqual(detections.calls.filter(call => call.text).map(call => call.text), ['Region: 80.0%', 'Region: 70.0%']);
    assert.ok(detections.calls.some(call => call.dash?.join() === '4,3'));
    assert.equal(render(null, [overlay]).calls.length, 0);
});

test('OCR reuses current label layout and invalidates width, font, toggles and frame changes', () => {
    const {reader, frame} = fixture();
    reader.detections[0].data.candidates[0].text = '東京 ' + '😀'.repeat(120);
    const result = resolved(frame, reader);
    const overlay = select('ocr', reader, {regionConfidence: true, textConfidence: true});
    let measurements = 0;
    const labels = [];
    const ctx = {save() {}, restore() {}, beginPath() {}, rect() {}, clip() {}, setLineDash() {}, strokeRect() {}, fillRect() {},
        measureText(text) { measurements++; return {width: Array.from(text).length * Number.parseFloat(this.font) / 2}; },
        fillText(text, x, y) { labels.push({text, x, y}); }};
    const target = {ctx, overlay, mapper: {display: {x: 25, y: 30, width: 400, height: 300}}};
    const draw = (value = result, owner = overlay) => {
        labels.length = 0;
        const before = measurements;
        ocr.render({...target, overlay: owner}, value, owner.settings);
        const used = measurements - before;
        for (const {text, x} of labels) {
            assert.ok(x - 4 >= 25);
            assert.ok(x + ctx.measureText(text).width + 4 <= 25 + target.mapper.display.width);
            assert.equal(/[\uD800-\uDFFF]/u.test(text), false);
        }
        return used;
    };
    const count = result.regions.length;
    assert.ok(draw() > count);
    assert.ok(labels[0].text.endsWith('Region: 70.0% · Text: 90.0%'));
    assert.equal(draw(), count, 'only text-chip drawing measures each cached label');
    overlay.settings.color = '#123456';
    overlay.settings.opacity = 0.4;
    assert.equal(draw(), count, 'non-layout styles preserve fitted labels');
    for (const change of [{textSize: '24'}, {text: false}, {regionConfidence: false}, {textConfidence: false},
        {text: true, regionConfidence: true, textConfidence: true}]) {
        Object.assign(overlay.settings, change);
        const used = draw();
        assert.ok(labels.length ? used > labels.length : used === 0);
        assert.equal(draw(), labels.length);
    }
    target.mapper.display.width = 180;
    assert.ok(draw() > count, 'narrowing the display refits labels');
    target.mapper.display.width = 800;
    assert.ok(draw() > count, 'widening the display restores available text');
    const next = fixture();
    next.reader.detections[0].data.candidates[0] = {text: 'UPDATED', confidence: 0.25};
    const nextResult = resolved(next.frame, next.reader);
    assert.ok(draw(nextResult) > count);
    assert.ok(labels.some(({text}) => text.startsWith('UPDATED') && text.includes('Text: 25.0%')));
    const other = select('ocr', next.reader, {textSize: '12'});
    assert.ok(draw(nextResult, other) > count);
    assert.equal(draw(nextResult), count, 'overlays keep independent layout caches');
});

test('zero and absent text scores never fall back to region confidence; saved confidence preferences migrate', () => {
    const {reader, frame} = fixture();
    reader.detections = [reader.detections[1]];
    const candidate = reader.detections[0].data.candidates[0];
    candidate.text = '';
    candidate.confidence = 0;
    const overlay = select('ocr', reader, {textConfidence: true});
    const labels = () => render(frame, [overlay], 800, 800, 640, 480).calls.filter(call => call.text).map(call => call.text);
    assert.deepEqual(labels(), ['Text: 0.0%']);
    overlay.settings.regionConfidence = true;
    assert.deepEqual(labels(), ['Region: 80.0% · Text: 0.0%']);
    delete candidate.confidence;
    assert.deepEqual(labels(), ['Region: 80.0%']);
    overlay.settings.regionConfidence = false;
    assert.deepEqual(labels(), []);
    const migrated = ocr.validateSettings({confidence: true, opacity: 0.3});
    assert.equal(migrated.regionConfidence, true);
    assert.equal(migrated.textConfidence, true);
    assert.equal(migrated.confidence, undefined);
    assert.equal(migrated.opacity, 0.3);
    assert.equal(ocr.validateSettings({confidence: true, textConfidence: false}).textConfidence, false);
    assert.equal(ocr.validateSettings({confidence: 'yes'}), null);
    const saved = {version: 2, overlays: [{...overlay, settings: {confidence: true, opacity: 0.3}}]};
    const restored = loadOverlays({getItem: () => JSON.stringify(saved)}).overlays[0];
    assert.equal(restored.settings.regionConfidence, true);
    assert.equal(restored.settings.textConfidence, true);
});
