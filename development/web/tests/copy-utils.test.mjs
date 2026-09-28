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
import test from 'node:test';

import {copyTextWithFeedback} from '../src/copy-utils.js';

test('copies through the document fallback when the Clipboard API is unavailable', async () => {
    const originalNavigator = Object.getOwnPropertyDescriptor(globalThis, 'navigator');
    const originalDocument = Object.getOwnPropertyDescriptor(globalThis, 'document');
    const icon = {};
    let buffer;
    let command;
    let restoredFocus = false;
    const button = {
        dataset: {},
        title: 'Copy output',
        getAttribute: () => 'Copy output',
        setAttribute() {},
        querySelector: () => icon,
        focus: () => { restoredFocus = true; },
    };

    Object.defineProperty(globalThis, 'navigator', {configurable: true, value: {}});
    Object.defineProperty(globalThis, 'document', {
        configurable: true,
        value: {
            activeElement: button,
            body: {appendChild: (element) => { buffer = element; }},
            createElement: () => ({
                style: {},
                focus() {},
                select() {},
                remove() { this.removed = true; },
            }),
            execCommand: (value) => {
                command = value;
                return true;
            },
        },
    });

    try {
        await copyTextWithFeedback(button, 'RPi output');
        clearTimeout(button.copyFeedbackTimer);
        assert.equal(buffer.value, 'RPi output');
        assert.equal(buffer.readOnly, true);
        assert.equal(buffer.removed, true);
        assert.equal(command, 'copy');
        assert.equal(button.dataset.copyState, 'copied');
        assert.equal(restoredFocus, true);
    } finally {
        if (originalNavigator) Object.defineProperty(globalThis, 'navigator', originalNavigator);
        else delete globalThis.navigator;
        if (originalDocument) Object.defineProperty(globalThis, 'document', originalDocument);
        else delete globalThis.document;
    }
});
