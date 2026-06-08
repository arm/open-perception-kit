const button = document.getElementById('videoFullscreenBtn');
const icon = document.getElementById('videoFullscreenIcon');
const controls = document.querySelector('.video-control-buttons');
const outputsButton = document.getElementById('fullscreenOutputsBtn');
const outputsIcon = document.getElementById('fullscreenOutputsIcon');

let isFullscreen = false;
let hideTimer = null;
let outputsAvailable = !document.body.classList.contains('output-panels-empty');
let outputsInFullscreen = (
    localStorage.getItem('pek-video:fullscreen-outputs:v1') ??
    localStorage.getItem('pek-video:fullscreen-metrics:v1')
) === 'true';

function notifyVideoLayoutChange() {
    window.dispatchEvent(new CustomEvent('video-layout-change', {
        detail: {
            isFullscreen,
            outputsInFullscreen,
            outputsAvailable,
        },
    }));
    requestAnimationFrame(() => {
        window.dispatchEvent(new CustomEvent('video-layout-change', {
            detail: {
                isFullscreen,
                outputsInFullscreen,
                outputsAvailable,
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
        if (isFullscreen && !controls?.matches(':hover, :focus-within')) {
            setControlsVisible(false);
        }
    }, delay);
}

function setOutputsInFullscreen(enabled) {
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

    notifyVideoLayoutChange();
}

function setOutputsAvailable(available) {
    outputsAvailable = available;

    if (outputsButton) {
        outputsButton.hidden = !outputsAvailable;
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

setOutputsInFullscreen(outputsInFullscreen);
setOutputsAvailable(outputsAvailable);

button?.addEventListener('click', () => {
    const nextFullscreen = !isFullscreen;
    setFullscreen(nextFullscreen);
    if (nextFullscreen) {
        button.blur();
    }
});

outputsButton?.addEventListener('click', () => {
    if (!outputsAvailable)
        return;

    setOutputsInFullscreen(!outputsInFullscreen);
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

document.addEventListener('mousemove', (event) => {
    if (!isFullscreen) return;

    const revealBand = Math.min(220, window.innerHeight * 0.22);
    if (event.clientY >= window.innerHeight - revealBand) {
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
