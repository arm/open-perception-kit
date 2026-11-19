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

import {BoxPromptSession, matchingPromptResult, promptPoint, validPromptBox} from './prompt-inputs.js';
import {calculateContainedRect} from './overlay-drawing.js';

export function initializePromptEditor({send, document = globalThis.document, window = globalThis.window}) {
    const panel = document.getElementById('promptPanel');
    const select = document.getElementById('promptTarget');
    const form = document.getElementById('promptForm');
    const apply = document.getElementById('promptApply');
    const cancel = document.getElementById('promptCancel');
    const draw = document.getElementById('promptDraw');
    const status = document.getElementById('promptStatus');
    const eligibility = document.getElementById('promptEligibility');
    const canvas = document.getElementById('promptPreviewCanvas');
    const video = document.getElementById('video');
    const keys = ['left', 'top', 'right', 'bottom'];
    const inputs = Object.fromEntries(keys.map(key => [key, document.getElementById(`prompt-${key}`)]));
    const session = new BoxPromptSession(send);
    let drawMode = false;
    let drag = null;
    const drafts = new Map();
    const listeners = [];

    function listen(element, type, listener) {
        element.addEventListener(type, listener);
        listeners.push(() => element.removeEventListener(type, listener));
    }

    function target() {
        return session.targets.find(item => item.target === select.value);
    }

    function box() {
        return Object.fromEntries(keys.map(key => [key, inputs[key].value === '' ? NaN : Number(inputs[key].value)]));
    }

    function viewReason() {
        if (!target()?.drawing_supported) {
            return target()?.reason || 'The display route is not qualified for box input.';
        }
        if (!video.videoWidth || !video.videoHeight || !canvas.clientWidth || !canvas.clientHeight) {
            return 'Waiting for video dimensions.';
        }
        if (document.body.classList.contains('video-feed-hidden')) {
            return 'Show the video feed to enter prompts.';
        }
        for (let element = video; element; element = element.parentElement) {
            const style = window.getComputedStyle(element);
            if ((style.transform && style.transform !== 'none') || (style.rotate && style.rotate !== 'none')
                || (style.scale && style.scale !== 'none' && style.scale !== '1')
                || (style.translate && style.translate !== 'none')
                || (style.zoom && style.zoom !== '1' && style.zoom !== 'normal')) {
                return 'Transformed or mirrored video is not supported for box input.';
            }
        }
        const videoStyle = window.getComputedStyle(video);
        if (videoStyle.objectPosition && videoStyle.objectPosition !== '50% 50%') {
            return 'Box input requires a centered video image.';
        }
        if (videoStyle.objectFit !== 'contain') {
            return 'Box input requires the contained full video image.';
        }
        return '';
    }

    function releaseDrag() {
        const previous = drag;
        drag = null;
        if (previous && canvas.hasPointerCapture?.(previous.pointerId)) {
            canvas.releasePointerCapture(previous.pointerId);
        }
    }

    function render() {
        const current = target();
        const state = session.state(select.value);
        const reason = session.sessionId ? viewReason() : 'Disconnected. Reconnect and Apply deliberately.';
        const blocked = Boolean(reason) || Boolean(state.pending);
        eligibility.textContent = reason || 'Box applies to successive incoming frames, not the previously displayed frame.';
        status.textContent = session.sessionId ? state.message : 'Disconnected. Prompts will not replay on reconnect.';
        select.disabled = !session.sessionId || Boolean(state.pending);
        apply.disabled = blocked || !validPromptBox(box());
        cancel.disabled = !session.sessionId || Boolean(state.pending) || !state.active;
        draw.disabled = blocked;
        for (const input of Object.values(inputs)) {
            input.disabled = blocked;
        }
        if (blocked) {
            drawMode = false;
            releaseDrag();
        }
        draw.setAttribute('aria-pressed', String(drawMode));
        canvas.classList.toggle('is-drawing', drawMode);
        const dpr = window.devicePixelRatio || 1;
        canvas.width = Math.max(1, Math.round(canvas.clientWidth * dpr));
        canvas.height = Math.max(1, Math.round(canvas.clientHeight * dpr));
        const context = canvas.getContext('2d');
        context.setTransform(dpr, 0, 0, dpr, 0, 0);
        context.clearRect(0, 0, canvas.clientWidth, canvas.clientHeight);
        const preview = box();
        if (!current || reason || !validPromptBox(preview)) {
            return;
        }
        const rect = calculateContainedRect(canvas.clientWidth, canvas.clientHeight, video.videoWidth, video.videoHeight);
        context.strokeStyle = '#f59e0b';
        context.lineWidth = 2;
        context.setLineDash([6, 4]);
        context.strokeRect(rect.x + preview.left * rect.width, rect.y + preview.top * rect.height,
            (preview.right - preview.left) * rect.width, (preview.bottom - preview.top) * rect.height);
    }

    function updateDraft() {
        drafts.set(select.value, box());
        const state = session.state(select.value);
        if (!state.pending) {
            state.message = 'Local draft. Apply to submit.';
        }
        render();
    }

    function selectTarget() {
        releaseDrag();
        const draft = drafts.get(select.value) || {left: 0.25, top: 0.25, right: 0.75, bottom: 0.75};
        for (const key of keys) {
            inputs[key].value = String(draft[key]);
        }
        drawMode = false;
        render();
    }

    function pointer(event, clamp) {
        const rect = canvas.getBoundingClientRect();
        return promptPoint(event.clientX - rect.left, event.clientY - rect.top,
            rect.width, rect.height, video.videoWidth, video.videoHeight, clamp);
    }

    listen(select, 'change', selectTarget);
    for (const input of Object.values(inputs)) {
        listen(input, 'input', updateDraft);
    }
    listen(form, 'submit', event => {
        event.preventDefault();
        if (apply.disabled || viewReason() || !validPromptBox(box())) {
            return;
        }
        try {
            session.command(select.value, 'submit', box());
        } catch (error) {
            session.state(select.value).message = error.message;
        }
        render();
    });
    listen(cancel, 'click', () => {
        if (!cancel.disabled) {
            session.command(select.value, 'cancel');
            render();
        }
    });
    listen(draw, 'click', () => {
        if (!draw.disabled) {
            drawMode = !drawMode;
            releaseDrag();
            render();
        }
    });
    listen(canvas, 'pointerdown', event => {
        if (!drawMode || event.button !== 0 || viewReason()) {
            return;
        }
        const start = pointer(event, false);
        if (!start) {
            return;
        }
        event.preventDefault();
        drag = {pointerId: event.pointerId, start};
        canvas.setPointerCapture(event.pointerId);
    });
    listen(canvas, 'pointermove', event => {
        if (!drag || event.pointerId !== drag.pointerId) {
            return;
        }
        const end = pointer(event, true);
        if (!end) {
            return;
        }
        const preview = {left: Math.min(drag.start.x, end.x), top: Math.min(drag.start.y, end.y),
            right: Math.max(drag.start.x, end.x), bottom: Math.max(drag.start.y, end.y)};
        for (const key of keys) {
            inputs[key].value = String(preview[key]);
        }
        updateDraft();
    });
    for (const type of ['pointerup', 'pointercancel', 'lostpointercapture']) {
        listen(canvas, type, () => { releaseDrag(); render(); });
    }
    listen(document, 'keydown', event => {
        if (event.key === 'Escape') {
            releaseDrag();
            drawMode = false;
            render();
        }
    });
    listen(window, 'prompt-control-message', ({detail}) => {
        if (detail.type === 'prompt_capabilities') {
            const previous = select.value;
            const previousSession = session.sessionId;
            if (!session.capabilities(detail)) {
                return;
            }
            if (previousSession !== session.sessionId) {
                drafts.clear();
            }
            select.replaceChildren();
            for (const item of session.targets) {
                const option = document.createElement('option');
                option.value = item.target;
                option.textContent = `${item.target} → ${item.destination_element}`;
                select.append(option);
            }
            if (session.targets.some(item => item.target === previous)) {
                select.value = previous;
            }
            panel.hidden = session.targets.length === 0;
            selectTarget();
        } else if (session.receive(detail)) {
            render();
        }
    });
    listen(window, 'control-connection', ({detail}) => {
        if (!detail.connected) {
            session.disconnect();
            drafts.clear();
            releaseDrag();
            drawMode = false;
            render();
        }
    });
    listen(window, 'video-frame-results', ({detail}) => {
        const state = session.state(select.value);
        if (!state.pending && matchingPromptResult(detail, target(), session.sessionId, state.active)) {
            state.message = `Matching result observed for revision ${state.active.revision}.`;
            render();
        }
    });
    for (const type of ['resize', 'video-layout-change']) {
        listen(window, type, render);
    }
    listen(document, 'fullscreenchange', render);
    listen(video, 'loadedmetadata', render);
    const observer = window.ResizeObserver ? new window.ResizeObserver(render) : null;
    observer?.observe(canvas);
    render();
    return {session, render, destroy() {
        releaseDrag();
        observer?.disconnect();
        for (const remove of listeners) {
            remove();
        }
    }};
}
