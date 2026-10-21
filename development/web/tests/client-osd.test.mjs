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
import {readFile} from 'node:fs/promises';
import test from 'node:test';
import {OVERLAY_STORAGE_KEY} from '../src/overlays.js';
import {outputSource, sourceKey} from '../src/overlay-sources.js';
import {OVERLAY_FEATURES} from '../src/overlay-features.js';
import {fixtureFeatures} from './overlay-test-utils.mjs';

// Minimal DOM double, following video-layout.test.mjs. Native focus and form
// validation are also exercised in the browser; this checks editor transitions.
class Element {
    constructor(tag) {
        this.tag = tag;
        this.children = [];
        this.listeners = new Map();
        this.selectedIndex = -1;
        this.hidden = false;
        this.isConnected = true;
        this.style = {setProperty(name, value) { this[name] = value; }};
    }
    addEventListener(type, listener) { this.listeners.set(type, listener); }
    fire(type, event = {}) { this.listeners.get(type)?.({preventDefault() {}, ...event}); }
    setAttribute(name, value) { this[name] = value; }
    append(...children) { this.children.push(...children); }
    replaceChildren(...children) { this.children = children; this.selectedIndex = -1; }
    get options() { return this.children; }
    add(option) { this.append(option); if (this.selectedIndex < 0) this.selectedIndex = 0; }
    get value() { return this.tag === 'select' ? this.options[this.selectedIndex]?.value || '' : this._value || ''; }
    set value(value) {
        if (this.tag === 'select') this.selectedIndex = this.options.findIndex((option) => option.value === value);
        else this._value = String(value);
    }
    get textContent() { return this._text || this.children.map((child) => child.textContent).join(''); }
    set textContent(text) { this._text = String(text); this.children = []; }
    focus() { document.activeElement = this; }
    showModal() { this.open = true; }
    close() { this.open = false; this.fire('close'); }
}

function find(element, predicate) {
    if (predicate(element)) return element;
    for (const child of element.children) {
        const result = find(child, predicate);
        if (result) return result;
    }
}

test('single-source UI, live discovery, saved editing and cancel/apply retain source identity and styles', async () => {
    // UI tests use non-drawing plugins, so the foundation is testable before
    // any production rendering feature is registered.
    const registered = [...OVERLAY_FEATURES];
    OVERLAY_FEATURES.splice(0, OVERLAY_FEATURES.length, ...fixtureFeatures());
    const originals = Object.fromEntries(['document', 'window', 'Option']
        .map((name) => [name, Object.getOwnPropertyDescriptor(globalThis, name)]));
    const html = await readFile(new URL('../content/index.html', import.meta.url), 'utf8');
    const elements = new Map([...html.matchAll(/<(\w+)\b[^>]*\bid="([^"]+)"[^>]*>/g)]
        .map(([markup, tag, id]) => {
            const element = new Element(tag);
            element.hidden = /\bhidden\b/.test(markup);
            return [id, element];
        }));
    const get = (id) => elements.get(id);
    const storage = new Map();
    const window = new Element('window');
    window.localStorage = {getItem: (key) => storage.get(key) ?? null, setItem: (key, value) => storage.set(key, value)};
    window.requestAnimationFrame = () => {};
    globalThis.window = window;
    globalThis.document = Object.assign(new Element('document'), {getElementById: get, createElement: (tag) => new Element(tag)});
    globalThis.Option = class extends Element {
        constructor(text, value) { super('option'); this.textContent = text; this.value = value; }
    };
    try {
        await import('../src/client-osd.js');
        const panel = get('clientOsdControlsPanel');
        const panelToggle = get('clientOsdControlsBtn');
        assert.equal(panel.hidden, true);
        panelToggle.fire('click');
        assert.equal(panel.hidden, false);
        assert.equal(panelToggle['aria-expanded'], 'true');
        document.fire('click', {target: get('models-container'), composedPath: () => [get('models-container'), document]});
        document.fire('keydown', {key: 'Escape'});
        assert.equal(panel.hidden, false);
        panelToggle.fire('click');
        assert.equal(panel.hidden, true);
        assert.equal(panelToggle['aria-pressed'], 'false');
        panelToggle.fire('click');
        const first = {payloadType: 'BoxDetections', model: 'YOLO26n', engine: 'ONNX', count: 0, detections: []};
        const second = {...first, engine: 'MNN'};
        const firstKey = sourceKey(outputSource(first));
        const secondKey = sourceKey(outputSource(second));
        const emit = (layers) => window.fire('metadata-message', {detail: {frame_results: {layers}}});
        const styleInput = (type) => find(get('overlayEditorSettings'), (element) => element.tag === 'input' && element.type === type);
        const setStyle = (type, value) => { const input = styleInput(type); input.value = value; input.fire('input'); };
        const saved = () => JSON.parse(storage.get(OVERLAY_STORAGE_KEY)).overlays;
        const cardButton = (text) => find(get('overlayList'), (element) => element.tag === 'button' && element.textContent === text);

        get('addOverlayBtn').fire('click');
        assert.equal(get('saveOverlayBtn').disabled, false);
        assert.ok(get('overlayFeature').options.some((option) => option.value === 'gaze'));
        emit([first, {...first}]);
        assert.equal(panel.hidden, false);
        assert.equal(get('overlayFeature').options[0].textContent, 'Bounding boxes');
        assert.equal(get('overlayOutput').value, firstKey);
        assert.equal(get('overlayOutput').options.length, 1);
        assert.equal(get('overlaySourceField').hidden, true);
        assert.equal(get('overlayOutput').disabled, true);
        assert.equal(get('overlayOutput').required, false);
        assert.equal(get('overlaySourceSummary').textContent, 'From YOLO26n');
        assert.equal(get('overlaySourceDetails').open, false);
        assert.ok(get('overlaySourceDetails').textContent.includes('BoxDetections'));
        assert.equal(get('saveOverlayBtn').disabled, false);
        setStyle('color', '#123456');
        setStyle('range', '0.42');
        const color = styleInput('color');

        emit([second, first]);
        assert.equal(get('overlaySourceField').hidden, false);
        assert.equal(get('overlaySourceSummary').hidden, true);
        assert.equal(get('overlayOutput').disabled, false);
        assert.equal(get('overlayOutput').required, true);
        assert.equal(get('overlayOutput').value, firstKey);
        assert.equal(styleInput('color'), color);
        assert.equal(styleInput('range').value, '0.42');
        assert.deepEqual(get('overlayOutput').options.map((option) => option.textContent), ['YOLO26n (ONNX)', 'YOLO26n (MNN)']);
        get('overlayForm').fire('submit');
        assert.deepEqual(saved(), [{featureId: 'bounding-boxes', selection: {primary: outputSource(first)},
            enabled: true, settings: {color: '#123456', opacity: 0.42}}]);
        assert.ok(get('overlayList').textContent.includes('Bounding boxes · YOLO26n (ONNX)'));
        const firstCard = get('overlayList').children[0];
        const disclosure = find(firstCard, (element) => element.className === 'overlay-toggle');
        const settings = find(firstCard, (element) => element.className === 'overlay-settings');
        assert.equal(settings.hidden, true);
        assert.equal(disclosure['aria-expanded'], 'false');
        assert.equal(firstCard.style['--overlay-color'], '#123456');
        assert.equal(firstCard.style['--overlay-foreground'], '#ffffff');
        disclosure.fire('click');
        assert.equal(settings.hidden, false);
        assert.equal(disclosure['aria-expanded'], 'true');
        const rowColor = find(settings, (element) => element.tag === 'input' && element.type === 'color');
        rowColor.value = '#ffffff';
        rowColor.fire('input');
        assert.equal(firstCard.style['--overlay-color'], '#ffffff');
        assert.equal(firstCard.style['--overlay-foreground'], '#000000');
        rowColor.value = '#123456';
        rowColor.fire('input');

        // A second saved overlay makes ordering and edits of the first observable.
        get('addOverlayBtn').fire('click');
        get('overlayOutput').value = secondKey;
        get('overlayOutput').fire('change');
        get('overlayForm').fire('submit');
        const checkbox = find(get('overlayList'), (element) => element.tag === 'input' && element.type === 'checkbox');
        assert.equal(checkbox.role, 'switch');
        assert.ok(find(get('overlayList'), (element) => element.className === 'model-toggle-track'));
        checkbox.checked = false;
        checkbox.fire('change');
        assert.equal(find(get('overlayList').children[0], (element) => element.className === 'overlay-settings').hidden, false);
        assert.equal(get('overlayList').children[0].style['--overlay-color'], '#123456');
        const beforeEdit = storage.get(OVERLAY_STORAGE_KEY);
        window.fire('metadata-connection', {detail: {connected: false}});
        assert.equal(get('overlayList').children[0].style['--overlay-color'], '#d1d5db');
        assert.equal(get('overlayList').children[0].style['--overlay-foreground'], '#4b5563');
        cardButton('Change source…').fire('click');
        assert.equal(get('overlayOutput').value, firstKey);
        assert.equal(get('overlaySourceField').hidden, true);
        assert.equal(styleInput('color').value, '#123456');
        assert.equal(get('overlayEditorHint').textContent, 'Waiting for source');
        setStyle('range', '0.15');
        emit([second]);
        assert.equal(get('overlaySourceField').hidden, false);
        assert.equal(get('overlayOutput').value, firstKey);
        assert.equal(styleInput('range').value, '0.15');
        window.fire('metadata-connection', {detail: {connected: false}});
        assert.equal(get('overlaySourceField').hidden, true);
        assert.equal(styleInput('range').value, '0.15');
        emit([first, second]);
        assert.equal(get('overlayList').children[0].style['--overlay-color'], '#123456');
        get('cancelOverlayBtn').fire('click');
        assert.equal(storage.get(OVERLAY_STORAGE_KEY), beforeEdit);

        cardButton('Change source…').fire('click');
        assert.equal(styleInput('range').value, '0.42');
        setStyle('range', '0.6');
        get('overlayForm').fire('submit');
        assert.deepEqual(saved().map((overlay) => sourceKey(overlay.selection.primary)), [firstKey, secondKey]);
        assert.equal(saved()[0].enabled, false);
        assert.deepEqual(saved()[0].settings, {color: '#123456', opacity: 0.6});

        const unsupported = {payloadType: 'TestUnsupportedPayload', model: 'Unsupported fixture', count: 0, detections: []};
        emit([first, unsupported]);
        get('addOverlayBtn').fire('click');
        get('overlayFeature').value = 'test-unsupported';
        get('overlayFeature').fire('change');
        assert.equal(get('overlaySourceField').hidden, true);
        assert.equal(get('saveOverlayBtn').disabled, true);
        assert.match(get('overlayEditorHint').textContent, /not supported/);
        get('cancelOverlayBtn').fire('click');
        assert.equal(saved().length, 2);

        const tracked = {...first, payloadType: 'ObjectTracks',
            producer: {instanceId: 'tracker/0', component: 'tracker'},
            detections: [{type: 'Rect', data: {trackId: '7'}}]};
        emit([first, tracked]);
        get('addOverlayBtn').fire('click');
        assert.ok(get('overlayFeature').options.some((option) => option.textContent === 'Tracked objects'));
        get('overlayFeature').value = 'tracked-objects';
        get('overlayFeature').fire('change');
        assert.match(get('overlayEditorHint').textContent, /trail history/);
        const trailsControl = find(get('overlayEditorSettings'), (element) => element.tag === 'label' && element.textContent === 'Trails');
        const trailsInput = find(trailsControl, (element) => element.tag === 'input');
        assert.equal(trailsInput.checked, true);
        trailsInput.checked = false;
        trailsInput.fire('change');
        assert.equal(get('overlayEditorHint').textContent, '');
        get('overlayForm').fire('submit');
        assert.equal(saved()[2].featureId, 'tracked-objects');
        assert.equal(saved()[2].settings.boxes, true);
        assert.equal(saved()[2].settings.trails, false);
        assert.equal(saved()[2].selection.primary.payloadType, 'ObjectTracks');

        // Configure gaze before its model has ever emitted metadata.
        get('addOverlayBtn').fire('click');
        get('overlayFeature').value = 'gaze';
        get('overlayFeature').fire('change');
        assert.equal(get('overlaySourceField').hidden, true);
        assert.equal(get('overlaySourceDetails').hidden, true);
        assert.equal(get('saveOverlayBtn').disabled, false);
        setStyle('range', '0.13');
        get('overlayForm').fire('submit');
        assert.equal(saved()[3].selection.primary, null);
        assert.equal(saved()[3].settings.opacity, 0.13);
        const gazeA = {payloadType: 'PoseEstimations', contentType: 'eyeYawPitch', model: 'gaze A', detections: []};
        const gazeB = {...gazeA, model: 'gaze B'};
        emit([gazeA, gazeB]);
        assert.equal(saved()[3].selection.primary, null);
        const gazeCard = get('overlayList').children.at(-1);
        assert.ok(gazeCard.textContent.includes('Choose a source'));
        find(gazeCard, (element) => element.tag === 'button' && element.textContent === 'Change source…').fire('click');
        assert.equal(get('overlayOutput').value, '');
        assert.equal(get('saveOverlayBtn').disabled, true);
        setStyle('range', '0.29');
        get('overlayOutput').value = sourceKey(outputSource(gazeB));
        get('overlayOutput').fire('change');
        assert.equal(styleInput('range').value, '0.29');
        get('cancelOverlayBtn').fire('click');
        assert.equal(saved()[3].selection.primary, null);
        assert.equal(saved()[3].settings.opacity, 0.13);

        // Automatic binding updates the saved source without resetting unsaved editor styles.
        get('addOverlayBtn').fire('click');
        get('overlayFeature').value = 'camera-contact';
        get('overlayFeature').fire('change');
        setStyle('range', '0.13');
        get('overlayForm').fire('submit');
        const contactCard = get('overlayList').children.at(-1);
        find(contactCard, (element) => element.tag === 'button' && element.textContent === 'Change source…').fire('click');
        setStyle('range', '0.27');
        const contact = {payloadType: 'Classifications', contentType: 'cameraContact', model: 'contact', detections: []};
        emit([contact]);
        assert.equal(get('overlayList').children.at(-1), contactCard);
        assert.equal(styleInput('range').value, '0.27');
        assert.deepEqual(saved()[4].selection.primary, outputSource(contact));
        assert.equal(saved()[4].settings.opacity, 0.13);
        get('cancelOverlayBtn').fire('click');
        assert.equal(saved()[4].settings.opacity, 0.13);
        window.fire('metadata-connection', {detail: {connected: false}});
        emit([{...contact, model: 'different contact source'}]);
        assert.deepEqual(saved()[4].selection.primary, outputSource(contact));
        assert.ok(contactCard.textContent.includes('Waiting for source'));
        // Removing a different row does not collapse an already expanded overlay.
        const remove = find(contactCard.children[0], (element) => element.className === 'overlay-remove');
        remove.fire('click');
        assert.equal(saved().length, 4);
        assert.equal(find(get('overlayList').children[0], (element) => element.className === 'overlay-settings').hidden, false);

        // A source from another feature must not silently select the first match.
        window.fire('metadata-connection', {detail: {connected: false}});
        emit([gazeA, first, second]);
        get('addOverlayBtn').fire('click');
        get('overlayFeature').value = 'gaze';
        get('overlayFeature').fire('change');
        assert.equal(get('overlayOutput').value, sourceKey(outputSource(gazeA)));
        get('overlayFeature').value = 'bounding-boxes';
        get('overlayFeature').fire('change');
        assert.equal(get('overlayOutput').value, '');
        assert.equal(get('overlayOutput').options[0].textContent, 'Choose a source');
        assert.equal(get('saveOverlayBtn').disabled, true);
        get('overlayForm').fire('submit');
        assert.equal(saved().length, 4);
        get('overlayOutput').value = secondKey;
        get('overlayOutput').fire('change');
        emit([gazeA, first, second, {...first, engine: 'third engine'}]);
        assert.equal(get('overlayOutput').value, secondKey);
        assert.equal(get('saveOverlayBtn').disabled, false);
        get('overlayForm').fire('submit');
        assert.equal(sourceKey(saved().at(-1).selection.primary), secondKey);
        get('addOverlayBtn').fire('click');
        get('overlayFeature').value = 'gaze';
        get('overlayFeature').fire('change');
        assert.equal(get('overlayOutput').value, sourceKey(outputSource(gazeA)));
        assert.equal(get('saveOverlayBtn').disabled, false);
        get('cancelOverlayBtn').fire('click');
    } finally {
        OVERLAY_FEATURES.splice(0, OVERLAY_FEATURES.length, ...registered);
        for (const [name, descriptor] of Object.entries(originals)) {
            if (descriptor) Object.defineProperty(globalThis, name, descriptor);
            else delete globalThis[name];
        }
    }
});
