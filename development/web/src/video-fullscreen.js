const button = document.getElementById('videoFullscreenBtn');
const icon = document.getElementById('videoFullscreenIcon');
const controls = document.querySelector('.video-control-buttons');
const videoWrapper = document.querySelector('.video-wrapper');
const outputsButton = document.getElementById('fullscreenOutputsBtn');
const outputsIcon = document.getElementById('fullscreenOutputsIcon');
const videoFeedButton = document.getElementById('toggleVideoFeedBtn');
const videoFeedIcon = document.getElementById('toggleVideoFeedIcon');

let isFullscreen = false;
let hideTimer = null;
let outputsAvailable = true;
let outputsInFullscreen = (
    localStorage.getItem('pek-video:fullscreen-outputs:v1') ??
    localStorage.getItem('pek-video:fullscreen-metrics:v1')
) === 'true';
let videoFeedHidden = localStorage.getItem('pek-video:feed-hidden:v1') === 'true';

function notifyVideoLayoutChange() {
    window.dispatchEvent(new CustomEvent('video-layout-change', {
        detail: {
            isFullscreen,
            outputsInFullscreen,
            outputsAvailable,
            videoFeedHidden,
        },
    }));
    requestAnimationFrame(() => {
        window.dispatchEvent(new CustomEvent('video-layout-change', {
            detail: {
                isFullscreen,
                outputsInFullscreen,
                outputsAvailable,
                videoFeedHidden,
            },
        }));
    });
}

function setControlsVisible(visible) {
    document.body.classList.toggle('video-fullscreen-controls-visible', visible);
}

function scheduleHideControls(delay = 1300) {
    window.clearTimeout(hideTimer);
    hideTimer = window.setTimeout(() => {
        if (
            isFullscreen &&
            !controls?.matches(':hover, :focus-within') &&
            !videoWrapper?.matches(':hover')
        ) {
            setControlsVisible(false);
        }
    }, delay);
}

function isPointerInsideVideoWrapper(event, margin = 0) {
    const rect = videoWrapper?.getBoundingClientRect();
    if (!rect) return false;

    return (
        event.clientX >= rect.left - margin &&
        event.clientX <= rect.right + margin &&
        event.clientY >= rect.top - margin &&
        event.clientY <= rect.bottom + margin
    );
}

function setVideoFeedHidden(hidden) {
    videoFeedHidden = hidden;
    document.body.classList.toggle('video-feed-hidden', videoFeedHidden);
    localStorage.setItem('pek-video:feed-hidden:v1', videoFeedHidden ? 'true' : 'false');

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

function setOutputsInFullscreen(enabled, { animate = true } = {}) {
    const update = () => {
        outputsInFullscreen = enabled;
        document.body.classList.toggle('fullscreen-outputs-enabled', outputsInFullscreen);
        localStorage.setItem('pek-video:fullscreen-outputs:v1', outputsInFullscreen ? 'true' : 'false');

        if (outputsButton) {
            outputsButton.setAttribute(
                'aria-label',
                outputsInFullscreen
                    ? 'Hide outputs in fullscreen'
                    : 'Show outputs in fullscreen'
            );
            outputsButton.setAttribute('aria-pressed', outputsInFullscreen ? 'true' : 'false');
            outputsButton.dataset.tooltip = outputsInFullscreen
                ? 'Hide outputs in fullscreen'
                : 'Show outputs in fullscreen';
        }

        if (outputsIcon) {
            outputsIcon.className = outputsInFullscreen
                ? 'fa-solid fa-gauge'
                : 'fa-solid fa-gauge video-gauge-icon--outline';
        }
    };

    if (animate && window.animateBottomDockHeightChange) {
        window.animateBottomDockHeightChange(update);
    } else {
        update();
    }

    notifyVideoLayoutChange();
}

function setOutputsAvailable(available) {
    outputsAvailable = available;

    if (outputsButton) {
        outputsButton.hidden = false;
    }

    notifyVideoLayoutChange();
}

function setFullscreen(nextFullscreen) {
    isFullscreen = nextFullscreen;
    document.body.classList.toggle('video-fullscreen', isFullscreen);

    if (button) {
        button.setAttribute('aria-label', isFullscreen ? 'Exit fullscreen video' : 'Fullscreen video');
        button.dataset.tooltip = isFullscreen ? 'Exit Fullscreen' : 'Fullscreen';
    }

    if (icon) {
        icon.className = isFullscreen
            ? 'fa-solid fa-down-left-and-up-right-to-center'
            : 'fa-solid fa-up-right-and-down-left-from-center';
    }

    setControlsVisible(isFullscreen);
    if (isFullscreen) {
        scheduleHideControls();
    } else {
        window.clearTimeout(hideTimer);
    }

    notifyVideoLayoutChange();
}

setVideoFeedHidden(videoFeedHidden);
setOutputsInFullscreen(outputsInFullscreen, { animate: false });
setOutputsAvailable(outputsAvailable);

button?.addEventListener('click', () => {
    const nextFullscreen = !isFullscreen;
    setFullscreen(nextFullscreen);
    if (nextFullscreen) {
        button.blur();
    }
});

outputsButton?.addEventListener('click', () => {
    setOutputsInFullscreen(!outputsInFullscreen);
});

videoFeedButton?.addEventListener('click', () => {
    setVideoFeedHidden(!videoFeedHidden);
});

window.addEventListener('output-panels-change', (event) => {
    setOutputsAvailable((event.detail?.visiblePanels?.length || 0) > 0);
});

controls?.addEventListener('mouseenter', () => {
    if (isFullscreen) {
        setControlsVisible(true);
        window.clearTimeout(hideTimer);
    }
});

controls?.addEventListener('mouseleave', () => {
    if (isFullscreen) scheduleHideControls(600);
});

videoWrapper?.addEventListener('mouseenter', () => {
    if (isFullscreen) {
        setControlsVisible(true);
        scheduleHideControls();
    }
});

videoWrapper?.addEventListener('mousemove', () => {
    if (isFullscreen) {
        setControlsVisible(true);
        scheduleHideControls();
    }
});

document.addEventListener('mousemove', (event) => {
    if (!isFullscreen) return;

    if (isPointerInsideVideoWrapper(event, 8)) {
        setControlsVisible(true);
        scheduleHideControls();
        return;
    }

    if (!controls?.matches(':hover, :focus-within')) {
        scheduleHideControls(250);
    }
});

document.addEventListener('keydown', (event) => {
    if (event.key === 'Escape' && isFullscreen) {
        setFullscreen(false);
    }
});
