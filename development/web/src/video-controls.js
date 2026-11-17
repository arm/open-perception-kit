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

// video-controls.js

import { ctrlSend } from "./ctrlws.js";

const playBtn = document.getElementById("playPauseBtn");
const playIcon = document.getElementById("playPauseIcon");
const playText = playBtn ? playBtn.querySelector(".video-control-text") : null;
const stopBtn = document.getElementById("stopPipelineBtn");
const pipelineStatus = document.getElementById("pipelineStatus");
const pipelineError = document.getElementById("pipelineError");
const video = document.getElementById("video");

let isPipelinePlaying = false;
let currentPipelineState = null;

const setBusy = (busy) => {
    if (!playBtn) return;

    const unavailable = busy || !currentPipelineState;
    playBtn.disabled = unavailable;
    if (stopBtn) stopBtn.disabled = unavailable || currentPipelineState.state === 'READY';
    playBtn.setAttribute('aria-busy', String(busy));
};

const renderPlayPause = () => {
    if (!playBtn || !playIcon || !playText) return;

    const isPaused = !isPipelinePlaying;
    document.body.classList.toggle("is-feed-paused", isPaused);
    playIcon.dataset.icon = isPaused ? 'play' : 'pause';
    const action = isPaused ? 'Play' : 'Pause';
    playText.textContent = action;
    playBtn.setAttribute("aria-label", `${action} pipeline`);
    playBtn.title = `${action} pipeline`;
    setBusy(Boolean(currentPipelineState?.busy || currentPipelineState?.pending));
};

const dispatchPauseState = () => {
    window.dispatchEvent(new CustomEvent("feed-pause-change", {
        detail: { paused: Boolean(window.OPK_FEED_PAUSED) },
    }));
};

const freezeFeedFrame = () => {
    if (!video) return;

    const wrapper = video.closest(".video-wrapper");
    if (!wrapper) return;

    let canvas = document.getElementById("videoFreezeFrame");
    if (!canvas) {
        canvas = document.createElement("canvas");
        canvas.id = "videoFreezeFrame";
        canvas.className = "video-freeze-frame";
        canvas.setAttribute("aria-hidden", "true");
        wrapper.appendChild(canvas);
    }

    const width = video.videoWidth || Math.max(1, Math.round(video.clientWidth || wrapper.clientWidth || 1280));
    const height = video.videoHeight || Math.max(1, Math.round(video.clientHeight || wrapper.clientHeight || 720));
    canvas.width = width;
    canvas.height = height;

    try {
        const context = canvas.getContext("2d");
        context?.drawImage(video, 0, 0, width, height);
    } catch (error) {
        console.warn("Video freeze frame failed", error);
    }

    canvas.classList.add("is-visible");
};

const resumeFeedFrame = async () => {
    const canvas = document.getElementById("videoFreezeFrame");
    canvas?.classList.remove("is-visible");

    try {
        if (video?.paused) {
            await video.play();
        }
    } catch (error) {
        console.warn("Video resume failed", error);
    }
};

export const setPipelineState = (state) => {
    const previousState = currentPipelineState;
    currentPipelineState = state;
    const isChanging = Boolean(state?.busy || state?.pending);
    const labels = {PLAYING: 'Running', PAUSED: 'Paused', READY: 'Stopped'};
    if (pipelineStatus) pipelineStatus.textContent = !state ? 'Disconnected'
        : isChanging ? 'Changing…' : state.error ? 'Error' : labels[state.state] || state.state;
    if (pipelineError) {
        pipelineError.textContent = state?.error || '';
        pipelineError.hidden = !state?.error;
    }

    if (!state) {
        renderPlayPause();
        return;
    }

    const wasPlaying = isPipelinePlaying;
    isPipelinePlaying = Boolean(state?.playing);
    window.OPK_FEED_PAUSED = !isPipelinePlaying;
    const pauseStateNeedsSync = !previousState || wasPlaying !== isPipelinePlaying;
    if (pauseStateNeedsSync) {
        if (isPipelinePlaying) void resumeFeedFrame();
        else freezeFeedFrame();
        dispatchPauseState();
    }
    if (state?.state === 'READY' && previousState?.state !== 'READY') {
        window.dispatchEvent(new CustomEvent('pipeline-stopped'));
    }
    renderPlayPause();
};

const requestState = (state) => {
    setBusy(true);
    try {
        ctrlSend({type: 'change_pipeline_state', state});
    } catch (error) {
        setPipelineState(null);
    }
};

const requestPlayPause = (event) => {
    event?.preventDefault();

    requestState(isPipelinePlaying ? 'PAUSED' : 'PLAYING');
};

stopBtn?.addEventListener('click', () => requestState('READY'));
playBtn?.addEventListener("click", requestPlayPause);
renderPlayPause();
