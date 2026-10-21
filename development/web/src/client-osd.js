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

import {renderOsd} from './osd-renderer.js';
import {decodeFrameResultsMessage} from './frame-results.js';

const canvas = document.getElementById('clientOsdCanvas');
const video = document.getElementById('video');
const toggleButton = document.getElementById('clientOsdControlsBtn');
const controlsPanel = document.getElementById('clientOsdControlsPanel');

const controlIds = {
    objects: 'clientOsdRenderObjects',
    faces: 'clientOsdRenderFaces',
    gaze: 'clientOsdRenderGaze',
    cameraContact: 'clientOsdRenderCameraContact',
    trackTraces: 'clientOsdRenderTrackTraces',
    classification: 'clientOsdRenderClassification',
    personStatus: 'clientOsdRenderPersonStatus',
};

const colorIds = {
    objects: 'clientOsdColorObjects',
    faces: 'clientOsdColorFaces',
    gaze: 'clientOsdColorGaze',
    cameraContact: 'clientOsdColorCameraContact',
    trackTraces: 'clientOsdColorTrackTraces',
    classification: 'clientOsdColorClassification',
    personStatus: 'clientOsdColorPersonStatus',
};

if (canvas && video) {
    let displayedFrameResults = null;

    function redraw() {
        renderOsd(canvas, video, displayedFrameResults, readRenderOptions());
    }

    window.addEventListener('video-frame-metadata', (event) => {
        // Null metadata also clears overlays when the WebRTC session closes.
        displayedFrameResults = null;
        if (event.detail) {
            try {
                displayedFrameResults = decodeFrameResultsMessage(event.detail).frame_results;
            } catch (error) {
                console.warn('Invalid video frame metadata', error);
            }
        }
        redraw();
    });

    controlsPanel?.addEventListener('input', redraw);
    controlsPanel?.addEventListener('change', redraw);
    window.addEventListener('resize', redraw);
    video.addEventListener('loadedmetadata', redraw);
    video.addEventListener('resize', redraw);
    if ('ResizeObserver' in window) {
        const resizeObserver = new ResizeObserver(redraw);
        resizeObserver.observe(canvas);
    }
    if (typeof video.requestVideoFrameCallback !== 'function') {
        console.warn('Synchronized overlays require requestVideoFrameCallback; overlays are disabled.');
    }
}

if (toggleButton && controlsPanel) {
    toggleButton.addEventListener('click', () => {
        const isOpen = toggleButton.getAttribute('aria-expanded') === 'true';
        setControlsOpen(!isOpen);
    });

    document.addEventListener('click', (event) => {
        if (
            controlsPanel.hidden
            || controlsPanel.contains(event.target)
            || toggleButton.contains(event.target)
        ) {
            return;
        }
        setControlsOpen(false);
    });

    document.addEventListener('keydown', (event) => {
        if (event.key === 'Escape') {
            setControlsOpen(false);
        }
    });
}

function readRenderOptions() {
    return {
        objects: isChecked(controlIds.objects),
        faces: isChecked(controlIds.faces),
        gaze: isChecked(controlIds.gaze),
        cameraContact: isChecked(controlIds.cameraContact),
        trackTraces: isChecked(controlIds.trackTraces),
        classification: isChecked(controlIds.classification),
        personStatus: isChecked(controlIds.personStatus),
        performance: false,
        colors: {
            objects: colorValue(colorIds.objects),
            faces: colorValue(colorIds.faces),
            gaze: colorValue(colorIds.gaze),
            cameraContact: colorValue(colorIds.cameraContact),
            trackTraces: colorValue(colorIds.trackTraces),
            classification: colorValue(colorIds.classification),
            personStatus: colorValue(colorIds.personStatus),
        },
    };
}

function isChecked(id) {
    const element = document.getElementById(id);
    return element ? element.checked : true;
}

function colorValue(id) {
    return document.getElementById(id)?.value;
}

function setControlsOpen(isOpen) {
    controlsPanel.hidden = !isOpen;
    toggleButton.setAttribute('aria-expanded', String(isOpen));
    toggleButton.setAttribute('aria-pressed', String(isOpen));
}
