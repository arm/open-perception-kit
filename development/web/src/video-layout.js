// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates

const sidebarButton = document.getElementById('sidebarVisibilityBtn');
const outputsButton = document.getElementById('outputsVisibilityBtn');
const videoFeedButton = document.getElementById('toggleVideoFeedBtn');
const videoFeedIcon = document.getElementById('toggleVideoFeedIcon');

let sidebarVisible = localStorage.getItem('opk-layout:sidebar-visible:v1') !== 'false';
let outputsVisible = localStorage.getItem('opk-video:outputs-visible:v1') !== 'false';
let videoFeedHidden = localStorage.getItem('opk-video:feed-hidden:v1') === 'true';

function notifyVideoLayoutChange() {
    const detail = { sidebarVisible, outputsVisible, videoFeedHidden };
    window.dispatchEvent(new CustomEvent('video-layout-change', { detail }));
    requestAnimationFrame(() => {
        window.dispatchEvent(new CustomEvent('video-layout-change', { detail }));
    });
}

function setSidebarVisible(visible) {
    sidebarVisible = visible;
    document.body.classList.toggle('sidebar-hidden', !sidebarVisible);
    localStorage.setItem('opk-layout:sidebar-visible:v1', sidebarVisible ? 'true' : 'false');
    sidebarButton?.setAttribute('aria-pressed', sidebarVisible ? 'true' : 'false');
    notifyVideoLayoutChange();
}

function setVideoFeedHidden(hidden) {
    videoFeedHidden = hidden;
    document.body.classList.toggle('video-feed-hidden', videoFeedHidden);
    localStorage.setItem('opk-video:feed-hidden:v1', videoFeedHidden ? 'true' : 'false');

    if (videoFeedButton) {
        videoFeedButton.setAttribute('aria-label', videoFeedHidden ? 'Show video feed' : 'Hide video feed');
        videoFeedButton.setAttribute('aria-pressed', videoFeedHidden ? 'true' : 'false');
        videoFeedButton.dataset.tooltip = videoFeedHidden ? 'Show video feed' : 'Hide video feed';
    }

    if (videoFeedIcon) {
        videoFeedIcon.className = videoFeedHidden ? 'fa-solid fa-eye' : 'fa-solid fa-eye-slash';
    }

    notifyVideoLayoutChange();
}

function setOutputsVisible(visible, { animate = true } = {}) {
    const update = () => {
        outputsVisible = visible;
        document.body.classList.toggle('outputs-hidden', !outputsVisible);
        localStorage.setItem('opk-video:outputs-visible:v1', outputsVisible ? 'true' : 'false');
        outputsButton?.setAttribute('aria-pressed', outputsVisible ? 'true' : 'false');
    };

    if (animate && window.animateBottomDockHeightChange) {
        window.animateBottomDockHeightChange(update);
    } else {
        update();
    }

    notifyVideoLayoutChange();
}

setSidebarVisible(sidebarVisible);
setVideoFeedHidden(videoFeedHidden);
setOutputsVisible(outputsVisible, { animate: false });

sidebarButton?.addEventListener('click', () => {
    setSidebarVisible(!sidebarVisible);
});

outputsButton?.addEventListener('click', () => {
    setOutputsVisible(!outputsVisible);
});

videoFeedButton?.addEventListener('click', () => {
    setVideoFeedHidden(!videoFeedHidden);
});
