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

import {setPipelineState} from "./video-controls.js"
import {enableAudioButton} from "./audio.js"
import {enableMicrophoneButton} from "./microphone.js"
import {modelsManager} from "./models.js"

const CTRL_PROTO = location.protocol === 'https:' ? 'wss' : 'ws';
const CTRL_HOST = location.hostname;

const CTRL_PORT = (window.OPK_CONFIG && window.OPK_CONFIG.ctrlPort) ||
                (location.port || (location.protocol === 'https:' ? 443 : 80));

const CTRL_URL = `${CTRL_PROTO}://${CTRL_HOST}:${CTRL_PORT}/ws`;

let ctrl = null;

let ctrlReconnectDelay = 1000;
const CTRL_RECONNECT_DELAY_MAX = 15000;
const CTRL_BACKOFF_FACTOR = 1.1;

function connectCtrl(manual = false) {
    if (ctrl && (ctrl.readyState === WebSocket.OPEN ||
                      ctrl.readyState === WebSocket.CONNECTING)) {
        return;
    }

    if (manual) {
        ctrlReconnectDelay = 1000;
    }

    ctrl = new WebSocket(CTRL_URL);

    ctrl.onopen = () => {
        ctrlReconnectDelay = 1000;

    };

    ctrl.onmessage = async (event) => {
        const data = JSON.parse(event.data);
        console.log("ctrl.onmessage: " + event.data);

        enableAudioButton(data.pipeline_state.audio);
        enableMicrophoneButton(data.pipeline_state.microphone);
        setPipelineState(data.pipeline_state);
        window.dispatchEvent(new CustomEvent('pipeline-state-change', {detail: data.pipeline_state}));
        window.dispatchEvent(new CustomEvent('models-change', {detail: data.models}));
        modelsManager.render(data.models, data.model_editor, data.pipeline_state);
    };

    ctrl.onerror = () => { ; };

    ctrl.onclose = () => {
        setPipelineState(null);
        modelsManager.render(modelsManager.serverModels, modelsManager.editor, null);
        window.dispatchEvent(new CustomEvent('pipeline-state-change', {detail: null}));
        enableMicrophoneButton(false);
        setTimeout(() => {
            ctrlReconnectDelay = Math.min(ctrlReconnectDelay * CTRL_BACKOFF_FACTOR, CTRL_RECONNECT_DELAY_MAX);
            connectCtrl();
        }, ctrlReconnectDelay);
    };
}

export function ctrlSend(obj) {
    const payload = JSON.stringify(obj);

    if (ctrl && ctrl.readyState === WebSocket.OPEN) {
        ctrl.send(payload);
        return true;
    }

    connectCtrl();
    throw new Error('Control connection lost; reconnect before making changes');
}

export function ctrlIsOpen() {
    return !!ctrl && ctrl.readyState === WebSocket.OPEN;
}

connectCtrl();
