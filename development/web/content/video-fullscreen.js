const button = document.getElementById('videoFullscreenBtn');
const icon = document.getElementById('videoFullscreenIcon');

let isFullscreen = false;
let hideTimer = null;

function setControlsVisible(visible) {
    document.body.classList.toggle('video-fullscreen-controls-visible', visible);
}

function scheduleHideControls(delay = 1300) {
    window.clearTimeout(hideTimer);
    hideTimer = window.setTimeout(() => {
        if (isFullscreen && document.activeElement !== button) {
            setControlsVisible(false);
        }
    }, delay);
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

button?.addEventListener('click', () => {
    const nextFullscreen = !isFullscreen;
    setFullscreen(nextFullscreen);
    if (nextFullscreen) {
        button.blur();
    }
});

button?.addEventListener('mouseenter', () => {
    if (isFullscreen) {
        setControlsVisible(true);
        window.clearTimeout(hideTimer);
    }
});

button?.addEventListener('mouseleave', () => {
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

    if (!button?.matches(':hover, :focus-visible')) {
        scheduleHideControls(250);
    }
});

document.addEventListener('keydown', (event) => {
    if (event.key === 'Escape' && isFullscreen) {
        setFullscreen(false);
    }
});
