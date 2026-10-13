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
import {defineFeature} from '../../src/overlay-feature.js';
import {indexFrame, outputSource, producerKey, sourceKey, trackKey} from '../../src/overlay-sources.js';
import {OVERLAY_FEATURES, resolveOverlay, overlayStatus} from '../../src/overlay-features.js';
import {createOverlay} from '../../src/overlays.js';
import {render, select, trackerFixture} from '../overlay-test-utils.mjs';

test('indexes retain multiple candidates without confusing source and relationship identity', () => {
    const {tracks, trails} = trackerFixture();
    const renamed = {...trails, model: 'another display label', engine: 'another engine'};
    const index = indexFrame({layers: [tracks, trails, renamed]});
    assert.equal(index.byPayload.get('TrackTraces').length, 2);
    assert.equal(index.byProducer.get(producerKey(outputSource(tracks))).length, 3);
    assert.equal(index.objects.get('10').length, 1);
    assert.equal(index.byTrack.get(trackKey(outputSource(tracks), '7')).length, 3);
    assert.notEqual(sourceKey(outputSource(renamed)), sourceKey(outputSource(trails)));
});

test('plugins own discovery, defaults and setting validation; unbound selections never guess', () => {
    const plugin = defineFeature({id: 'test-contract', label: 'Test contract', accepts: (source) => source.payloadType === 'TestPayload',
        controls: [{key: 'enabledPart', label: 'Part', type: 'checkbox', value: true}], defaults: {enabledPart: false}, render() {}});
    OVERLAY_FEATURES.push(plugin);
    try {
        const source = outputSource({payloadType: 'TestPayload', model: 'source'});
        assert.deepEqual(plugin.discover(new Map([[sourceKey(source), source]])), [source]);
        assert.deepEqual(plugin.defaults(source), {enabledPart: false});
        assert.equal(plugin.validateSettings({enabledPart: 'yes'}), null);
        const overlay = createOverlay(plugin.id);
        assert.deepEqual(overlay.settings, {enabledPart: false});
        assert.equal(resolveOverlay(indexFrame(null), overlay).state, 'waiting');
        const second = {...source, model: 'second'};
        assert.equal(overlayStatus(indexFrame(null), overlay, new Map([[sourceKey(source), source], [sourceKey(second), second]])), 'Choose a source');
        assert.equal(resolveOverlay(indexFrame(null), {...overlay, featureId: 'absent-plugin'}).state, 'unsupported');
    } finally { OVERLAY_FEATURES.pop(); }
});

test('a plugin implementing the contract directly can render without dispatcher changes', () => {
    let rendered = false;
    const plugin = {id: 'test-direct', label: 'Direct', controls: [], defaults: {}, discover: (catalog) => [...catalog.values()],
        validateSettings: () => ({}),
        resolve: (index, selection) => ({state: 'ready', items: [], output: index.outputs.get(sourceKey(selection.primary))}),
        render(target, resolved) { rendered = Boolean(target.canvas && resolved.output); }};
    OVERLAY_FEATURES.push(plugin);
    try {
        const {tracks} = trackerFixture();
        assert.deepEqual(select(plugin.id, tracks).settings, {});
        const context = render({layers: [tracks]}, [select(plugin.id, tracks)]);
        assert.equal(rendered, true);
        assert.equal(context.globalAlpha, 1);
    } finally { OVERLAY_FEATURES.pop(); }
});
