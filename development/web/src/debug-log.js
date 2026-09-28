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

import { copyTextWithFeedback, setCopyButtonAvailable } from './copy-utils.js?v=icon-copy-buttons-20260608';

const copyButton = document.getElementById('copyDebugLogBtn');
const logEl = document.getElementById('log');

function getLogText() {
    if (!logEl) return '';

    return Array.from(logEl.querySelectorAll('.log-line'))
        .map((line) => line.textContent.trim())
        .filter(Boolean)
        .join('\n');
}

async function copyLog() {
    if (!copyButton) return;

    const logText = getLogText();
    await copyTextWithFeedback(copyButton, logText, 'Copy');
}

function updateCopyButtonState() {
    setCopyButtonAvailable(copyButton, !!getLogText());
}

copyButton?.addEventListener('click', copyLog);

if (logEl) {
    new MutationObserver(updateCopyButtonState).observe(logEl, {
        childList: true,
        subtree: true,
        characterData: true,
    });
}

updateCopyButtonState();
