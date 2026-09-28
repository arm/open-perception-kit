/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

test('sidebar and outputs are independent rightmost toggles', async () => {
    const globals = ['document', 'window', 'localStorage', 'requestAnimationFrame', 'CustomEvent'];
    const originals = Object.fromEntries(
        globals.map((name) => [name, Object.getOwnPropertyDescriptor(globalThis, name)]),
    );
    const classes = new Set();
    const storage = new Map();
    const button = () => ({
        dataset: {},
        listeners: {},
        addEventListener(type, listener) { this.listeners[type] = listener; },
        setAttribute(name, value) { this[name] = value; },
    });
    const elements = {
        sidebarVisibilityBtn: button(),
        outputsVisibilityBtn: button(),
        toggleVideoFeedBtn: button(),
        toggleVideoFeedIcon: {},
    };

    Object.defineProperties(globalThis, {
        document: {configurable: true, value: {
            body: {classList: {
                contains: (name) => classes.has(name),
                toggle: (name, enabled) => enabled ? classes.add(name) : classes.delete(name),
            }},
            getElementById: (id) => elements[id],
        }},
        window: {configurable: true, value: {dispatchEvent() {}}},
        localStorage: {configurable: true, value: {
            getItem: (key) => storage.get(key) ?? null,
            setItem: (key, value) => storage.set(key, value),
        }},
        requestAnimationFrame: {configurable: true, value: (callback) => callback()},
        CustomEvent: {configurable: true, value: class {
            constructor(type, init) { this.type = type; this.detail = init?.detail; }
        }},
    });

    try {
        await import(`../src/video-layout.js?test=${Date.now()}`);
        elements.sidebarVisibilityBtn.listeners.click();
        assert.equal(classes.has('sidebar-hidden'), true);
        assert.equal(classes.has('outputs-hidden'), false);

        elements.outputsVisibilityBtn.listeners.click();
        assert.equal(classes.has('sidebar-hidden'), true);
        assert.equal(classes.has('outputs-hidden'), true);

        const html = await readFile(new URL('../content/index.html', import.meta.url), 'utf8');
        const ids = ['clientOsdControlsBtn', 'toggleVideoFeedBtn', 'sidebarVisibilityBtn', 'outputsVisibilityBtn'];
        const positions = ids.map((id) => html.indexOf(`id="${id}"`));
        assert.deepEqual(positions, [...positions].sort((left, right) => left - right));
    } finally {
        for (const name of globals) {
            if (originals[name]) Object.defineProperty(globalThis, name, originals[name]);
            else delete globalThis[name];
        }
    }
});
