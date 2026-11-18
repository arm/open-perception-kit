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
import test, {after} from 'node:test';
import {collectOutputs, outputSource, sourceDetails, sourceKey, sourceLabel, sourceLabels} from '../src/overlay-sources.js';
import {addDefaultOverlays, bindPendingOverlays, createOverlay, loadOverlays, saveOverlays, OVERLAY_STORAGE_KEY} from '../src/overlays.js';
import {OVERLAY_FEATURES} from '../src/overlay-features.js';
import {fixtureFeatures} from './overlay-test-utils.mjs';

const registered = [...OVERLAY_FEATURES];
OVERLAY_FEATURES.splice(0, OVERLAY_FEATURES.length, ...fixtureFeatures());
after(() => OVERLAY_FEATURES.splice(0, OVERLAY_FEATURES.length, ...registered));

const layer = {
    payloadType: 'BoxDetections', contentType: 'genericObject', model: 'model',
    inferElementId: 'infer', producer: {instanceId: 'infer/parser', component: 'parser', implementation: 'Yolo'},
    detections: [], count: 0,
};

test('output identity survives frame reordering, empty results, and restart; isolates all provenance', () => {
    const source = outputSource(layer);
    const key = sourceKey(source);
    const cropped = {...layer, detections: [{data: {uuid: 'new-id'}}], count: 1};
    const frame = {layers: [cropped, {payloadType: 'FrameContext'}, layer]};
    const outputs = collectOutputs(frame);
    assert.equal(outputs.size, 1);
    assert.equal(outputs.get(key).layers.length, 2);
    assert.deepEqual([...collectOutputs({layers: frame.layers.toReversed()}).keys()], [key]);
    assert.equal(collectOutputs(null).size, 0);
    assert.ok(collectOutputs({layers: [structuredClone(layer)]}).has(key));
    for (const field of Object.keys(source)) {
        assert.notEqual(sourceKey({...source, [field]: 'different'}), key, field);
    }
    assert.ok(collectOutputs({layers: [{...layer, producer: null}]}).size === 1);
});

test('saved preferences round trip independently of live frames and availability', () => {
    let raw = null;
    const storage = {getItem: () => raw, setItem: (_key, value) => { raw = value; }};
    assert.deepEqual(loadOverlays(storage), {overlays: [], defaultSources: new Set(), notice: ''});
    const overlays = [{...createOverlay('bounding-boxes', outputSource(layer)), enabled: false, settings: {color: '#123456', opacity: 0.25}}];
    assert.equal(saveOverlays(storage, overlays), '');
    assert.deepEqual(loadOverlays(storage).overlays, overlays);
    assert.equal(collectOutputs(null).size, 0);
    assert.deepEqual(loadOverlays(storage).overlays, overlays);
    assert.ok(!raw.includes('detections'));
});

test('fresh defaults use real sources once, preserve edits and removals, and leave existing setups alone', () => {
    const fixtures = [...OVERLAY_FEATURES];
    OVERLAY_FEATURES.splice(0, OVERLAY_FEATURES.length, ...registered);
    try {
        const records = new Map();
        const storage = {getItem: key => records.get(key) ?? null, setItem: (key, value) => records.set(key, value)};
        let {overlays, defaultSources} = loadOverlays(storage);
        const sources = [layer, {...layer, producer: {...layer.producer, instanceId: 'other/parser'}},
            ...['TrackTraces', 'Classifications', 'PersonPresences', 'CameraContacts', 'GazeMeasurements',
                'PerformanceMeasurements', 'SegmentationMasks', 'SemanticSegmentationMasks', 'InstanceSegmentationMasks',
                'ObjectTracks', 'UnknownPayload'].map(payloadType => ({...layer, payloadType})),
            {...layer, producer: null}].map(outputSource);
        const catalog = new Map(sources.map(source => [sourceKey(source), source]));
        assert.equal(addDefaultOverlays(overlays, catalog, defaultSources), true);
        assert.deepEqual(overlays.map(overlay => overlay.featureId), ['bounding-boxes', 'bounding-boxes',
            'movement-trails', 'classifications', 'classifications', 'camera-contact', 'gaze', 'performance',
            'background-replacement', 'semantic-segmentation', 'instance-segmentation']);
        assert.ok(overlays.every(overlay => overlay.enabled && overlay.selection.primary.producerInstanceId));
        assert.deepEqual(overlays.slice(0, 2).map(overlay => overlay.selection.primary), sources.slice(0, 2));
        overlays[0].enabled = false;
        overlays[0].settings.color = '#123456';
        overlays.splice(1, 1);
        const customized = structuredClone(overlays);
        assert.equal(addDefaultOverlays(overlays, catalog, defaultSources), false);
        assert.equal(saveOverlays(storage, overlays, defaultSources), '');
        ({overlays, defaultSources} = loadOverlays(storage));
        assert.equal(addDefaultOverlays(overlays, new Map(), defaultSources), false);
        assert.equal(addDefaultOverlays(overlays, catalog, defaultSources), false);
        assert.deepEqual(overlays, customized);

        // A newly enabled model gets a new row without rebinding existing selections.
        const later = {...sources[0], producerInstanceId: 'later/parser'};
        catalog.set(sourceKey(later), later);
        assert.equal(addDefaultOverlays(overlays, catalog, defaultSources), true);
        assert.deepEqual(overlays.at(-1).selection.primary, later);
        assert.deepEqual(overlays.slice(0, -1), customized);
        assert.equal(saveOverlays(storage, [], defaultSources), '');
        const empty = loadOverlays(storage);
        assert.equal(addDefaultOverlays(empty.overlays, catalog, empty.defaultSources), false);
        assert.deepEqual(empty.overlays, []);

        // No new defaults are injected into existing v3 or migrated v2 configurations.
        for (const version of [2, 3]) {
            records.clear();
            records.set(`opk-overlays:v${version}`, JSON.stringify({version, overlays: []}));
            const existing = loadOverlays(storage);
            assert.equal(existing.defaultSources, null);
            assert.equal(addDefaultOverlays(existing.overlays, catalog, existing.defaultSources), false);
        }
        const manual = [createOverlay('face-circles', sources[0]), createOverlay('gaze')];
        assert.equal(addDefaultOverlays(manual, new Map([[sourceKey(sources[0]), sources[0]]]), new Set()), true);
        assert.equal(manual.length, 2, 'a manual presentation must not get an automatic duplicate');
        const gaze = sources.find(source => source.payloadType === 'GazeMeasurements');
        addDefaultOverlays(manual, new Map([[sourceKey(gaze), gaze]]), new Set());
        assert.equal(manual.length, 2, 'a waiting manual overlay retains its existing binding rules');
        assert.match(saveOverlays(undefined, overlays, defaultSources), /could not be saved/);
    } finally {
        OVERLAY_FEATURES.splice(0, OVERLAY_FEATURES.length, ...fixtures);
    }
});

test('corrupt, unsupported, and unavailable storage cannot break the editor', () => {
    for (const raw of ['{', 'null', '{"version":4,"overlays":[]}', '{"version":3,"overlays":{}}']) {
        const restored = loadOverlays({getItem: () => raw});
        assert.deepEqual(restored.overlays, []);
        assert.ok(restored.notice);
    }
    assert.ok(loadOverlays(undefined).notice);
    assert.ok(saveOverlays(undefined, []).length > 0);
    const valid = {...createOverlay('bounding-boxes', outputSource(layer)), settings: {color: '#abcdef', opacity: 1}};
    const invalid = [null, {}, {...valid, selection: null}, {...valid, enabled: 'yes'},
        {...valid, settings: {color: 'red', opacity: 1}},
        {...valid, settings: {color: '#abcdef', opacity: 2}},
        {...valid, settings: {color: '#abcdef', opacity: '0.5'}}];
    const result = loadOverlays({getItem: () => JSON.stringify({version: 3, overlays: [...invalid, valid]})});
    assert.deepEqual(result.overlays, [valid]);
    assert.ok(result.notice);
});

test('source presentation uses model or producer metadata without altering identities', () => {
    const source = outputSource({...layer, model: 'YOLO26n'});
    const original = structuredClone(source);
    const key = sourceKey(source);
    assert.equal(sourceLabel(source), 'YOLO26n');
    assert.deepEqual([...sourceLabels([source, {...source}])], [[key, 'YOLO26n']]);
    assert.equal(sourceLabel({...source, model: ''}), 'Yolo');
    assert.equal(sourceLabel(outputSource({payloadType: 'BoxDetections'})), 'Unspecified source');
    assert.equal(sourceLabel(outputSource({payloadType: 'PerformanceMeasurements'})), 'Pipeline performance');
    assert.deepEqual(Object.fromEntries(sourceDetails(source)), {
        'Payload type': 'BoxDetections', Engine: 'Not provided',
        Model: 'YOLO26n', 'Inference element': 'infer', 'Label family': 'Not provided',
        'Producer instance': 'infer/parser', Component: 'parser', Implementation: 'Yolo',
    });
    assert.deepEqual(source, original);
    assert.equal(sourceKey(source), key);
});

test('duplicate model names get only the shortest distinguishing metadata qualifier', () => {
    const first = {...outputSource(layer), engine: 'ONNX', producerInstanceId: 'very-long-inference-element/first-parser'};
    const second = {...first, engine: 'MNN', producerInstanceId: 'very-long-inference-element/second-parser'};
    const labels = sourceLabels([first, second]);
    assert.equal(labels.get(sourceKey(first)), 'model (ONNX)');
    assert.equal(labels.get(sourceKey(second)), 'model (MNN)');
    assert.deepEqual(sourceLabels([second, first]), labels);
    // Different Show selections do not require extra qualifiers.
    assert.equal(sourceLabels([first, {...first, payloadType: 'ObjectTracks'}]).get(sourceKey(first)), 'model');
});

test('qualifiers use a minimal combination when no single field distinguishes sources', () => {
    const sources = ['A', 'B'].flatMap((engine) => ['x', 'y'].map((inferElementId) => ({
        ...outputSource(layer), engine, inferElementId,
        producerInstanceId: `same-long-producer-for-${inferElementId}`,
    })));
    assert.deepEqual([...sourceLabels(sources).values()], ['model (A · x)', 'model (A · y)', 'model (B · x)', 'model (B · y)']);
});

test('missing and delimiter-containing metadata are displayed honestly and remain distinguishable', () => {
    const source = outputSource(layer);
    const labels = sourceLabels([source, {...source, engine: 'ONNX'}]);
    assert.equal(labels.get(sourceKey(source)), 'model (Engine not provided)');
    assert.equal(labels.get(sourceKey({...source, engine: 'ONNX'})), 'model (ONNX)');
    const unusual = [source, {...source, engine: 'Engine not provided'}];
    assert.equal(new Set(sourceLabels(unusual).values()).size, 2);
});

test('v2 migration preserves explicit renderer, source, order and settings and drops retired selectors', () => {
    const originals = [createOverlay('face-circles', outputSource(layer)),
        createOverlay('tracked-objects', outputSource({...layer, payloadType: 'ObjectTracks'})),
        createOverlay('gaze', outputSource({...layer, payloadType: 'GazeMeasurements'}))];
    originals[0].enabled = false;
    originals[0].settings = {color: '#123456', opacity: 0.4};
    const old = JSON.stringify({version: 2, overlays: originals.map(overlay => ({...overlay,
        selection: {primary: {...overlay.selection.primary, contentType: 'humanFace'}}}))});
    const records = new Map([['opk-overlays:v2', old]]);
    const storage = {getItem: key => records.get(key) ?? null, setItem: (key, value) => records.set(key, value)};
    const migrated = loadOverlays(storage);
    assert.match(migrated.notice, /migrated/);
    assert.deepEqual(migrated.overlays, originals);
    assert.equal(records.get('opk-overlays:v2'), old);
    assert.equal(JSON.parse(records.get(OVERLAY_STORAGE_KEY)).version, 3);
    assert.deepEqual(loadOverlays(storage).overlays, originals);
    const failedWrite = loadOverlays({getItem: key => key === 'opk-overlays:v2' ? old : null,
        setItem() { throw new Error('denied'); }});
    assert.deepEqual(failedWrite.overlays, originals);
    assert.match(failedWrite.notice, /could not be saved/);
});

test('feature-specific settings are validated, while unknown feature records survive round trips', () => {
    const record = createOverlay('tracked-objects', outputSource({...layer, payloadType: 'ObjectTracks'}));
    assert.equal(record.settings.trails, true);
    let raw = JSON.stringify({version: 3, overlays: [{...record, settings: {...record.settings, trails: 'yes'}}]});
    const storage = {getItem: () => raw, setItem: (_key, value) => { raw = value; }};
    assert.equal(loadOverlays(storage).overlays.length, 0);
    const future = {...record, featureId: 'future-view', settings: {mode: 'new-style'}};
    assert.equal(saveOverlays(storage, [future]), '');
    assert.deepEqual(loadOverlays(storage).overlays, [future]);
});

test('saved typed roots survive without subtype-based relabelling', () => {
    const entries = [['performance', 'PerformanceMeasurements'], ['gaze', 'GazeMeasurements'],
        ['camera-contact', 'CameraContacts'], ['classifications', 'PersonPresences']];
    const originals = entries.map(([featureId, payloadType]) => createOverlay(featureId, outputSource({payloadType})));
    let raw = null;
    const storage = {getItem: () => raw, setItem: (_key, value) => { raw = value; }};
    assert.equal(saveOverlays(storage, originals), '');
    assert.deepEqual(loadOverlays(storage).overlays, originals);
});

test('waiting overlays persist without invented provenance and bind only once to a unique compatible source', () => {
    const overlay = createOverlay('gaze');
    overlay.enabled = false;
    overlay.settings = {color: '#123456', opacity: 0.37};
    let raw = null;
    const storage = {getItem: () => raw, setItem: (_key, value) => { raw = value; }};
    assert.equal(saveOverlays(storage, [overlay]), '');
    const restored = loadOverlays(storage).overlays;
    assert.deepEqual(restored, [overlay]);
    assert.equal(restored[0].selection.primary, null);
    const source = outputSource({payloadType: 'GazeMeasurements', contentType: 'eyeYawPitch', model: 'gaze'});
    const invalid = {...source, payloadType: 'UnrelatedPayload'};
    const catalog = new Map([[sourceKey(invalid), invalid]]);
    assert.equal(bindPendingOverlays(restored, catalog), false);
    catalog.set(sourceKey(source), source);
    assert.equal(bindPendingOverlays(restored, catalog), true);
    assert.deepEqual(restored[0].selection.primary, source);
    assert.deepEqual(restored[0].settings, {color: '#123456', opacity: 0.37});
    assert.equal(restored[0].enabled, false);
    catalog.clear();
    const other = {...source, model: 'another-model'};
    catalog.set(sourceKey(other), other);
    assert.equal(bindPendingOverlays(restored, catalog), false);
    assert.deepEqual(restored[0].selection.primary, source);
    assert.equal(saveOverlays(storage, restored), '');
    assert.deepEqual(loadOverlays(storage).overlays, restored);
});

test('waiting selections do not guess between multiple producers or bind unsupported features', () => {
    const first = outputSource(layer);
    const second = {...first, model: 'another-model'};
    const pending = createOverlay('bounding-boxes');
    const catalog = new Map([[sourceKey(first), first], [sourceKey(second), second]]);
    assert.equal(bindPendingOverlays([pending], catalog), false);
    assert.equal(pending.selection.primary, null);
    const source = outputSource({payloadType: 'TestUnsupportedPayload'});
    const unsupported = createOverlay('test-unsupported');
    assert.equal(bindPendingOverlays([unsupported], new Map([[sourceKey(source), source]])), false);
});

test('saved bounding overlays keep their explicit renderer regardless of retired tags', () => {
    const faces = ['BoxDetections', 'ObjectTracks'].map((payloadType) => ({
        ...createOverlay('bounding-boxes', outputSource({...layer, payloadType})),
        enabled: false, settings: {color: '#654321', opacity: 0.37},
    }));
    const objectBox = createOverlay('bounding-boxes', outputSource(layer));
    const waiting = createOverlay('bounding-boxes');
    let raw = JSON.stringify({version: 3, overlays: [...faces, objectBox, waiting]});
    let writes = 0;
    const storage = {getItem: () => raw, setItem: (_key, value) => { raw = value; writes++; }};
    const restored = loadOverlays(storage);
    assert.equal(restored.notice, '');
    assert.deepEqual(restored.overlays, [
        ...faces, objectBox, waiting,
    ]);
    assert.equal(writes, 0);
    assert.deepEqual(loadOverlays(storage).overlays, restored.overlays);
    assert.equal(writes, 0);
});

test('circle and rectangular presentations can select the same typed box producer', () => {
    const face = outputSource({...layer, contentType: 'humanFace'});
    const circle = createOverlay('face-circles');
    const boxes = createOverlay('bounding-boxes');
    assert.equal(bindPendingOverlays([circle, boxes], new Map([[sourceKey(face), face]])), true);
    assert.deepEqual(circle.selection.primary, face);
    assert.deepEqual(boxes.selection.primary, face);
});
