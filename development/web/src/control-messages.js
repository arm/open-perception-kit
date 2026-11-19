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

export function parseControlMessage(raw) {
    let data;
    try {
        data = JSON.parse(raw);
    } catch {
        return null;
    }
    if (!data || typeof data !== 'object' || Array.isArray(data)) {
        return null;
    }
    if (['prompt_capabilities', 'prompt_ack', 'prompt_error', 'prompt_failure'].includes(data.type)) {
        return data.version === 1 ? {kind: 'prompt', data} : null;
    }
    if (data.type !== undefined) {
        return null;
    }
    return {kind: 'status', data};
}

export function sendConnected(socket, message, openState) {
    if (!socket || socket.readyState !== openState) {
        return false;
    }
    try {
        socket.send(JSON.stringify(message));
        return true;
    } catch {
        return false;
    }
}
