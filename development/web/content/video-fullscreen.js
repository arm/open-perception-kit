const button = document.getElementById('videoFullscreenBtn');
const icon = document.getElementById('videoFullscreenIcon');
const controls = document.querySelector('.video-control-buttons');
const metricsButton = document.getElementById('fullscreenMetricsBtn');
const metricsIcon = document.getElementById('fullscreenMetricsIcon');

let isFullscreen = false;
let hideTimer = null;
let metricsInFullscreen = localStorage.getItem('pek-video:fullscreen-metrics:v1') === 'true';

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

function setMetricsInFullscreen(enabled) {
    metricsInFullscreen = enabled;
    document.body.classList.toggle('fullscreen-metrics-enabled', metricsInFullscreen);
    localStorage.setItem('pek-video:fullscreen-metrics:v1', metricsInFullscreen ? 'true' : 'false');

    if (metricsButton) {
        metricsButton.setAttribute(
            'aria-label',
            metricsInFullscreen
                ? 'Disable performance metrics in fullscreen'
                : 'Enable performance metrics in fullscreen'
        );
        metricsButton.setAttribute('aria-pressed', metricsInFullscreen ? 'true' : 'false');
        metricsButton.dataset.tooltip = metricsInFullscreen
            ? 'Disable performance metrics in fullscreen'
            : 'Enable performance metrics in fullscreen';
    }

    if (metricsIcon) {
        metricsIcon.className = metricsInFullscreen
            ? 'fa-solid fa-gauge'
            : 'fa-solid fa-gauge video-gauge-icon--outline';
    }
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
}

setMetricsInFullscreen(metricsInFullscreen);

button?.addEventListener('click', () => {
    const nextFullscreen = !isFullscreen;
    setFullscreen(nextFullscreen);
    if (nextFullscreen) {
        button.blur();
    }
});

metricsButton?.addEventListener('click', () => {
    setMetricsInFullscreen(!metricsInFullscreen);
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
