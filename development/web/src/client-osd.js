import {renderOsd, resizeCanvasToDisplaySize} from './osd-renderer.js';

const canvas = document.getElementById('clientOsdCanvas');
const video = document.getElementById('video');
const toggleButton = document.getElementById('clientOsdControlsBtn');
const controlsPanel = document.getElementById('clientOsdControlsPanel');

const latest = {
    perception: null,
};

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
    window.addEventListener('metadata-message', (event) => {
        latest.perception = event.detail?.perception || null;
    });

    window.addEventListener('resize', () => resizeCanvasToDisplaySize(canvas));
    video.addEventListener('loadedmetadata', () => resizeCanvasToDisplaySize(canvas));
    video.addEventListener('resize', () => resizeCanvasToDisplaySize(canvas));

    requestAnimationFrame(renderFrame);
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

function renderFrame() {
    renderOsd(canvas, video, latest.perception, readRenderOptions());
    requestAnimationFrame(renderFrame);
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
