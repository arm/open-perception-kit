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

import {renderOsd} from '../src/osd-renderer.js';
import {indexFrame, outputSource} from '../src/overlay-sources.js';
import {createOverlay} from '../src/overlays.js';
import {defineFeature, STYLE_CONTROLS} from '../src/overlay-feature.js';

// Non-drawing plugins isolate UI/persistence tests from production features.
// The historical IDs also exercise migration of existing browser preferences.
export function fixtureFeatures() {
    return [
        defineFeature({id: 'bounding-boxes', label: 'Bounding boxes', accepts: (s) => ['BoxDetections', 'ObjectTracks'].includes(s.payloadType) && s.contentType !== 'humanFace', render() {}}),
        defineFeature({id: 'face-circles', label: 'Face circles', accepts: (s) => ['BoxDetections', 'ObjectTracks'].includes(s.payloadType) && s.contentType === 'humanFace', render() {}}),
        defineFeature({id: 'gaze', label: 'Gaze', accepts: (s) => s.payloadType === 'PoseEstimations' && s.contentType === 'eyeYawPitch', render() {}}),
        defineFeature({id: 'camera-contact', label: 'Camera contact', accepts: (s) => s.payloadType === 'Classifications', render() {}}),
        defineFeature({id: 'test-unsupported', label: 'Unsupported fixture', accepts: (s) => s.payloadType === 'TestUnsupportedPayload', reason: 'Fixture rendering is not supported.'}),
        defineFeature({id: 'tracked-objects', label: 'Tracked objects', accepts: (s) => s.payloadType === 'ObjectTracks', render() {},
            controls: [{key: 'boxes', label: 'Boxes', type: 'checkbox', value: true}, {key: 'trails', label: 'Trails', type: 'checkbox', value: true}, ...STYLE_CONTROLS],
            resolve: (result, _index, settings) => settings.trails ? {...result, state: 'partial', message: 'Waiting for trail history.'} : result}),
    ];
}

export function select(featureId, layer, settings = {}) {
    const overlay = createOverlay(featureId, outputSource(layer));
    Object.assign(overlay.settings, settings);
    return overlay;
}

export function render(frame, overlays, width = 640, height = 480, videoWidth = width, videoHeight = height) {
    const calls = [];
    const stack = [];
    const ctx = {
        calls, globalAlpha: 1,
        setTransform() {}, clearRect() {}, beginPath() {}, closePath() {}, moveTo() {}, lineTo() {}, fill() {}, fillRect() {},
        save() { stack.push({globalAlpha: this.globalAlpha, strokeStyle: this.strokeStyle, fillStyle: this.fillStyle}); },
        restore() { Object.assign(this, stack.pop()); },
        setLineDash(value) { calls.push({dash: value}); },
        arc(...args) { calls.push({arc: args}); },
        stroke() { calls.push({stroke: this.strokeStyle, alpha: this.globalAlpha}); },
        strokeRect(x, y, w, h) { calls.push({rect: [x, y, w, h], color: this.strokeStyle, alpha: this.globalAlpha}); },
        measureText: (text) => ({width: text.length * 8}),
        fillText(text, x, y) { calls.push({text, x, y}); },
    };
    const original = globalThis.window;
    globalThis.window = {devicePixelRatio: 1};
    try {
        renderOsd({clientWidth: width, clientHeight: height, width, height, getContext: () => ctx},
            {videoWidth, videoHeight}, indexFrame(frame), overlays, {now: 0});
    } finally {
        if (original === undefined) delete globalThis.window;
        else globalThis.window = original;
    }
    return ctx;
}

export function trackerFixture() {
    const tracks = {payloadType: 'ObjectTracks', contentType: 'genericObject', model: 'Tracker', engine: 'std',
        inferElementId: 'opktracker0', producer: {instanceId: 'opktracker0/opktracker0', component: 'gstreamer/opktracker', implementation: 'Tracker'},
        detections: [{type: 'Rect', data: {uuid: '10', trackId: '7', x: 20, y: 30, width: 40, height: 50}}]};
    const trails = {...tracks, payloadType: 'TrackTraces', contentType: 'trackTrace',
        detections: [{type: 'TrackTrace', data: {trackId: '7', points: [{x: 1, y: 2}, {x: 40, y: 55}]}}]};
    return {tracks, trails};
}

export function faceFixture() {
    return {payloadType: 'BoxDetections', contentType: 'humanFace',
        detections: [{type: 'Rect', data: {uuid: 'face', x: 10, y: 20, width: 30, height: 40}}]};
}
