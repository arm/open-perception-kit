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
import {readFile} from 'node:fs/promises';
import {initializePromptEditor} from '../src/prompt-editor.js';

class Element {
    constructor(tag = 'div') {
        this.tag = tag;
        this.listeners = new Map();
        this.children = [];
        this.disabled = false;
        this.hidden = false;
        this._value = '';
        const classes = new Set();
        this.classList = {contains: name => classes.has(name), toggle(name, value) {
            if (value) { classes.add(name); } else { classes.delete(name); }
        }};
    }
    addEventListener(name, callback) {
        if (!this.listeners.has(name)) { this.listeners.set(name, new Set()); }
        this.listeners.get(name).add(callback);
    }
    removeEventListener(name, callback) { this.listeners.get(name)?.delete(callback); }
    fire(name, event = {}) {
        for (const callback of this.listeners.get(name) || []) {
            callback({preventDefault() {}, ...event});
        }
    }
    setAttribute(name, value) { this[name] = value; }
    replaceChildren() { this.children = []; this._value = ''; }
    append(child) { this.children.push(child); }
    get value() { return this._value || (this.tag === 'select' ? this.children[0]?.value || '' : ''); }
    set value(value) { this._value = String(value); }
}

async function fixture() {
    const html = await readFile('content/index.html', 'utf8');
    const elements = new Map([...html.matchAll(/<(\w+)\b[^>]*\bid="([^"]+)"[^>]*>/g)]
        .map(([markup, tag, id]) => {
            const element = new Element(tag);
            element.hidden = /\bhidden\b/.test(markup);
            const value = markup.match(/\bvalue="([^"]*)"/);
            if (value) { element.value = value[1]; }
            return [id, element];
        }));
    const get = id => elements.get(id);
    const document = Object.assign(new Element(), {getElementById: get, createElement: tag => new Element(tag), body: new Element()});
    const contextCalls = [];
    const context = Object.fromEntries(['setTransform', 'clearRect', 'setLineDash', 'strokeRect']
        .map(name => [name, (...args) => contextCalls.push({name, args})]));
    Object.assign(get('promptPreviewCanvas'), {clientWidth: 800, clientHeight: 800, getContext: () => context,
        getBoundingClientRect: () => ({left: 0, top: 0, width: 800, height: 800}),
        setPointerCapture(id) { this.captured = id; }, hasPointerCapture(id) { return this.captured === id; },
        releasePointerCapture() { this.captured = null; }});
    Object.assign(get('video'), {videoWidth: 640, videoHeight: 480, parentElement: document.body});
    const window = Object.assign(new Element(), {devicePixelRatio: 2,
        getComputedStyle: element => ({objectFit: 'contain', transform: element.transform || 'none'})});
    const sent = [];
    let connected = true;
    const editor = initializePromptEditor({send: message => { if (!connected) { return false; } sent.push(message); return true; }, document, window});
    const capabilities = session => window.fire('prompt-control-message', {detail: {type: 'prompt_capabilities', version: 1,
        session_id: session, targets: [{target: 'prompts', destination_element: 'mobileSam', inference_instance: 'sam',
            source_name: 'pipelineVideoFrame', drawing_supported: true, format: 'BPRM', format_major: 1, format_minor: 0}]}});
    const ack = accepted => {
        const wire = sent.at(-1);
        const pending = editor.session.state(wire.target).pending;
        window.fire('prompt-control-message', {detail: {type: 'prompt_ack', version: 1, target: wire.target,
            command_id: wire.command_id, command: wire.command, session_id: editor.session.sessionId,
            accepted, reason: 'Rejected update', request_id: String(pending.request.requestId), revision: String(pending.request.revision), basis: 'bus'}});
    };
    return {get, editor, window, document, sent, capabilities, ack, contextCalls,
        setConnected(value) { connected = value; }};
}

test('numeric keyboard-equivalent form submits once, retains accepted request on rejection, and cancels explicitly', async () => {
    const f = await fixture();
    try {
        assert.equal(f.get('promptPanel').hidden, true);
        assert.equal(f.get('promptApply').disabled, true);
        f.capabilities('session-a');
        assert.equal(f.get('promptPanel').hidden, false);
        assert.equal(f.get('promptApply').disabled, false);
        f.get('promptForm').fire('submit');
        assert.equal(f.sent.length, 1);
        assert.equal(f.get('promptApply').disabled, true);
        f.get('promptForm').fire('submit');
        assert.equal(f.sent.length, 1);
        f.ack(true);
        assert.match(f.get('promptStatus').textContent, /Accepted revision 1/);
        assert.equal(f.get('promptCancel').disabled, false);
        f.get('prompt-left').value = '';
        f.get('prompt-left').fire('input');
        assert.equal(f.get('promptApply').disabled, true);
        f.get('prompt-left').value = '0.3';
        f.get('prompt-left').fire('input');
        f.get('promptForm').fire('submit');
        f.ack(false);
        assert.equal(f.editor.session.state('prompts').active.revision, 1n);
        assert.match(f.get('promptStatus').textContent, /rejected/);
        f.get('promptCancel').fire('click');
        assert.equal(f.sent.at(-1).command, 'cancel');
        f.ack(true);
        assert.equal(f.get('promptCancel').disabled, true);
        assert.match(f.get('promptStatus').textContent, /Already paired work may finish/);
    } finally { f.editor.destroy(); }
});

test('draw mode uses contained CSS coordinates at DPR two, ignores bars, cancels capture and never submits on move', async () => {
    const f = await fixture();
    try {
        f.capabilities('session-a');
        const canvas = f.get('promptPreviewCanvas');
        f.get('promptDraw').fire('click');
        assert.equal(canvas.classList.contains('is-drawing'), true);
        canvas.fire('pointerdown', {button: 0, pointerId: 1, clientX: 400, clientY: 50});
        assert.equal(canvas.captured, undefined);
        canvas.fire('pointerdown', {button: 0, pointerId: 1, clientX: 640, clientY: 580});
        canvas.fire('pointermove', {pointerId: 1, clientX: 160, clientY: 220});
        assert.equal(Number(f.get('prompt-left').value), 0.2);
        assert.equal(Number(f.get('prompt-top').value), 0.2);
        assert.equal(Number(f.get('prompt-right').value), 0.8);
        assert.equal(Number(f.get('prompt-bottom').value), 0.8);
        assert.equal(f.sent.length, 0);
        assert.equal(canvas.width, 1600);
        assert.ok(f.contextCalls.some(call => call.name === 'setTransform' && call.args[0] === 2));
        canvas.fire('pointercancel');
        assert.equal(canvas.captured, null);
        f.document.fire('keydown', {key: 'Escape'});
        assert.equal(canvas.classList.contains('is-drawing'), false);
        assert.equal(f.sent.length, 0);
    } finally { f.editor.destroy(); }
});

test('unknown transforms, missing video dimensions and disconnected/reconnected state cannot submit or replay', async () => {
    const f = await fixture();
    try {
        f.capabilities('session-a');
        f.get('video').transform = 'matrix(-1,0,0,1,0,0)';
        f.window.fire('resize');
        assert.equal(f.get('promptApply').disabled, true);
        assert.match(f.get('promptEligibility').textContent, /mirrored/);
        f.get('video').transform = 'none';
        f.editor.session.targets[0].drawing_supported = false;
        f.editor.render();
        assert.equal(f.get('promptApply').disabled, true);
        f.editor.session.targets[0].drawing_supported = true;
        f.get('video').videoWidth = 0;
        f.get('video').fire('loadedmetadata');
        assert.equal(f.get('promptDraw').disabled, true);
        f.get('video').videoWidth = 640;
        f.get('video').fire('loadedmetadata');
        f.get('promptForm').fire('submit');
        f.ack(true);
        f.setConnected(false);
        f.window.fire('control-connection', {detail: {connected: false}});
        assert.equal(f.get('promptApply').disabled, true);
        assert.equal(f.get('promptCancel').disabled, true);
        assert.equal(f.editor.session.state('prompts').active, null);
        f.get('promptForm').fire('submit');
        assert.equal(f.sent.length, 1);
        f.setConnected(true);
        f.capabilities('session-b');
        assert.equal(f.sent.length, 1);
        assert.equal(f.get('promptApply').disabled, false);
        assert.equal(f.editor.session.sessionId, 'session-b');
    } finally { f.editor.destroy(); }
});
