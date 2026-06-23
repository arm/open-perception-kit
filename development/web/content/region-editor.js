const drawButton = document.getElementById('addRegionBtn');
const removeButton = document.getElementById('removeRegionBtn');
const applyCropButton = document.getElementById('applyRoiCropBtn');
const clearCropButton = document.getElementById('clearRoiCropBtn');
const wrapper = document.querySelector('.video-wrapper');
const video = document.getElementById('video');
const canvas = document.getElementById('regionCanvas');
const overlay = document.getElementById('regionOverlay');

const MIN_CROP_SIZE = 0.02;
const roiPipelineAvailable = Boolean(window.PEK_CONFIG?.supervised && window.PEK_CONFIG?.roiPipeline);

let region = null;
let draftStart = null;
let draftEnd = null;
let drawing = false;
let drawingPointerId = null;
let editDrag = null;
let renderFrame = null;
let latestInferenceOutput = null;
let cropInProgress = false;
let activeCrop = null;

function wrapperRect() {
    return wrapper?.getBoundingClientRect() || new DOMRect(0, 0, 0, 0);
}

function videoContentRect() {
    const bounds = wrapperRect();
    const naturalWidth = video?.videoWidth || 16;
    const naturalHeight = video?.videoHeight || 9;
    const naturalRatio = naturalWidth / naturalHeight;
    const wrapperRatio = bounds.width / Math.max(1, bounds.height);

    let width = bounds.width;
    let height = bounds.height;
    let left = 0;
    let top = 0;

    if (wrapperRatio > naturalRatio) {
        width = bounds.height * naturalRatio;
        left = (bounds.width - width) / 2;
    } else {
        height = bounds.width / naturalRatio;
        top = (bounds.height - height) / 2;
    }

    return { left, top, width, height };
}

function svgPoint(point) {
    const rect = videoContentRect();
    return {
        x: rect.left + point.x * rect.width,
        y: rect.top + point.y * rect.height,
    };
}

function svgRect(rectangle) {
    const topLeft = svgPoint({ x: rectangle.x, y: rectangle.y });
    const bottomRight = svgPoint({
        x: rectangle.x + rectangle.width,
        y: rectangle.y + rectangle.height,
    });

    return {
        x: topLeft.x,
        y: topLeft.y,
        width: bottomRight.x - topLeft.x,
        height: bottomRight.y - topLeft.y,
    };
}

function eventPoint(event, clampToContent = false) {
    const bounds = wrapperRect();
    const rect = videoContentRect();
    const x = event.clientX - bounds.left;
    const y = event.clientY - bounds.top;

    if (
        x < rect.left ||
        x > rect.left + rect.width ||
        y < rect.top ||
        y > rect.top + rect.height
    ) {
        if (clampToContent) {
            return {
                x: clamp((x - rect.left) / rect.width),
                y: clamp((y - rect.top) / rect.height),
            };
        }
        return null;
    }

    return {
        x: (x - rect.left) / rect.width,
        y: (y - rect.top) / rect.height,
    };
}

function clamp(value, min = 0, max = 1) {
    return Math.min(Math.max(value, min), max);
}

function normaliseRectangle(start, end) {
    if (!start || !end)
        return null;

    const left = clamp(Math.min(start.x, end.x));
    const right = clamp(Math.max(start.x, end.x));
    const top = clamp(Math.min(start.y, end.y));
    const bottom = clamp(Math.max(start.y, end.y));
    const width = right - left;
    const height = bottom - top;

    if (width < MIN_CROP_SIZE || height < MIN_CROP_SIZE)
        return null;

    return { x: left, y: top, width, height };
}

function regionHandles(rectangle) {
    if (!rectangle)
        return [];

    const left = rectangle.x;
    const right = rectangle.x + rectangle.width;
    const top = rectangle.y;
    const bottom = rectangle.y + rectangle.height;
    const middleX = (left + right) / 2;
    const middleY = (top + bottom) / 2;

    return [
        { id: 'nw', x: left, y: top },
        { id: 'n', x: middleX, y: top },
        { id: 'ne', x: right, y: top },
        { id: 'e', x: right, y: middleY },
        { id: 'se', x: right, y: bottom },
        { id: 's', x: middleX, y: bottom },
        { id: 'sw', x: left, y: bottom },
        { id: 'w', x: left, y: middleY },
        { id: 'move', x: middleX, y: middleY },
    ];
}

function resizedRegion(startRegion, handle, startPoint, point) {
    if (!startRegion || !startPoint || !point)
        return startRegion;

    if (handle === 'move') {
        const nextX = clamp(
            startRegion.x + point.x - startPoint.x,
            0,
            1 - startRegion.width
        );
        const nextY = clamp(
            startRegion.y + point.y - startPoint.y,
            0,
            1 - startRegion.height
        );
        return {
            ...startRegion,
            x: nextX,
            y: nextY,
        };
    }

    let left = startRegion.x;
    let right = startRegion.x + startRegion.width;
    let top = startRegion.y;
    let bottom = startRegion.y + startRegion.height;

    if (handle.includes('w')) {
        left = clamp(point.x, 0, right - MIN_CROP_SIZE);
    }
    if (handle.includes('e')) {
        right = clamp(point.x, left + MIN_CROP_SIZE, 1);
    }
    if (handle.includes('n')) {
        top = clamp(point.y, 0, bottom - MIN_CROP_SIZE);
    }
    if (handle.includes('s')) {
        bottom = clamp(point.y, top + MIN_CROP_SIZE, 1);
    }

    return {
        x: left,
        y: top,
        width: right - left,
        height: bottom - top,
    };
}

function pointInRegion(point, rectangle = region) {
    if (!point || !rectangle)
        return false;

    return (
        point.x >= rectangle.x &&
        point.x <= rectangle.x + rectangle.width &&
        point.y >= rectangle.y &&
        point.y <= rectangle.y + rectangle.height
    );
}

function element(name, attributes = {}) {
    const node = document.createElementNS('http://www.w3.org/2000/svg', name);
    for (const [key, value] of Object.entries(attributes)) {
        node.setAttribute(key, value);
    }
    return node;
}

function textElement(value, attributes = {}) {
    const node = element('text', attributes);
    node.textContent = value;
    return node;
}

function sourceDimensions(output = latestInferenceOutput) {
    const layers = Array.isArray(output?.layers) ? output.layers : [];
    for (const layer of layers) {
        const detections = Array.isArray(layer.detections) ? layer.detections : [];
        for (const detection of detections) {
            const data = detection?.data || {};
            if (data.originalWidth && data.originalHeight) {
                return { width: data.originalWidth, height: data.originalHeight };
            }
        }
    }

    return {
        width: video?.videoWidth || 1,
        height: video?.videoHeight || 1,
    };
}

function detectionSamplePoints(x, y, width, height, dimensions) {
    const frameWidth = Math.max(1, dimensions.width);
    const frameHeight = Math.max(1, dimensions.height);
    const left = x / frameWidth;
    const right = (x + width) / frameWidth;
    const top = y / frameHeight;
    const bottom = (y + height) / frameHeight;
    const middleX = (left + right) / 2;
    const middleY = (top + bottom) / 2;

    return [
        { x: left, y: top },
        { x: middleX, y: top },
        { x: right, y: top },
        { x: right, y: middleY },
        { x: right, y: bottom },
        { x: middleX, y: bottom },
        { x: left, y: bottom },
        { x: left, y: middleY },
        { x: middleX, y: middleY },
    ];
}

function detectionRect(detection, dimensions) {
    const data = detection?.data || {};
    if (detection?.type !== 'Rect')
        return null;

    const width = Number(data.width);
    const height = Number(data.height);
    const x = Number(data.x);
    const y = Number(data.y);
    if (![x, y, width, height].every(Number.isFinite))
        return null;

    const videoRect = videoContentRect();
    const scaleX = videoRect.width / Math.max(1, dimensions.width);
    const scaleY = videoRect.height / Math.max(1, dimensions.height);

    return {
        x: videoRect.left + x * scaleX,
        y: videoRect.top + y * scaleY,
        width: width * scaleX,
        height: height * scaleY,
        label: data.text || (Number.isFinite(data.confidence) ? `${Math.round(data.confidence * 100)}%` : ''),
        samplePoints: detectionSamplePoints(x, y, width, height, dimensions),
    };
}

function detectionAllowedByRegion(detection, dimensions = sourceDimensions()) {
    if (!region)
        return true;

    const rect = detectionRect(detection, dimensions);
    if (!rect)
        return false;

    return rect.samplePoints.every((point) => pointInRegion(point));
}

function filterInferenceOutput(output) {
    if (!region || !Array.isArray(output?.layers))
        return output;

    const dimensions = sourceDimensions(output);
    return {
        ...output,
        layers: output.layers
            .map((layer) => {
                const detections = Array.isArray(layer.detections) ? layer.detections : [];
                const filteredDetections = detections.filter((detection) => (
                    detectionAllowedByRegion(detection, dimensions)
                ));

                return {
                    ...layer,
                    detections: filteredDetections,
                    count: filteredDetections.length,
                };
            })
            .filter((layer) => (
                layer.model || layer.contentType || layer.engine
            )),
    };
}

window.PEK_REGION_FILTER = {
    hasRegions: () => Boolean(region),
    filterInferenceOutput,
};

function renderDetections() {
    if (!overlay)
        return;

    const layers = Array.isArray(latestInferenceOutput?.layers) ? latestInferenceOutput.layers : [];
    const dimensions = sourceDimensions();

    for (const layer of layers) {
        const detections = Array.isArray(layer.detections) ? layer.detections : [];
        for (const detection of detections) {
            const rect = detectionRect(detection, dimensions);
            if (!rect)
                continue;
            if (!detectionAllowedByRegion(detection, dimensions))
                continue;

            overlay.appendChild(element('rect', {
                class: 'region-detection-box',
                x: rect.x,
                y: rect.y,
                width: rect.width,
                height: rect.height,
                rx: 0,
                ry: 0,
            }));

            if (rect.label) {
                overlay.appendChild(textElement(rect.label, {
                    class: 'region-detection-label',
                    x: rect.x + 3,
                    y: Math.max(14, rect.y - 5),
                }));
            }
        }
    }
}

function sizeCanvasForDisplay() {
    if (!canvas)
        return null;

    const bounds = wrapperRect();
    const ratio = window.devicePixelRatio || 1;
    const width = Math.max(1, Math.round(bounds.width * ratio));
    const height = Math.max(1, Math.round(bounds.height * ratio));

    if (canvas.width !== width || canvas.height !== height) {
        canvas.width = width;
        canvas.height = height;
    }

    return { bounds, ratio };
}

function drawVideoFrame() {
    if (!video || !canvas || !region) {
        renderFrame = null;
        return;
    }

    const sizing = sizeCanvasForDisplay();
    const context = canvas.getContext('2d');
    if (!sizing || !context) {
        renderFrame = null;
        return;
    }

    const { bounds, ratio } = sizing;
    const rect = videoContentRect();
    const selectedRect = svgRect(region);
    context.save();
    context.setTransform(ratio, 0, 0, ratio, 0, 0);
    context.clearRect(0, 0, bounds.width, bounds.height);
    context.fillStyle = '#000000';
    context.fillRect(0, 0, bounds.width, bounds.height);

    try {
        context.filter = 'grayscale(1) brightness(0.72)';
        context.drawImage(video, rect.left, rect.top, rect.width, rect.height);
        context.filter = 'none';
        context.beginPath();
        context.rect(selectedRect.x, selectedRect.y, selectedRect.width, selectedRect.height);
        context.clip();
        context.drawImage(video, rect.left, rect.top, rect.width, rect.height);
    } catch (error) {
        console.warn('Region canvas render failed', error);
        context.filter = 'none';
        context.clearRect(0, 0, bounds.width, bounds.height);
    }

    context.restore();
    renderFrame = requestAnimationFrame(drawVideoFrame);
}

function renderRegionCanvas() {
    if (!video || !canvas)
        return;

    const hasRegion = Boolean(region);
    video.classList.toggle('has-regions', hasRegion);
    canvas.classList.toggle('is-visible', hasRegion);

    if (!hasRegion) {
        if (renderFrame) {
            cancelAnimationFrame(renderFrame);
            renderFrame = null;
        }
        canvas.getContext('2d')?.clearRect(0, 0, canvas.width, canvas.height);
        return;
    }

    if (!renderFrame) {
        renderFrame = requestAnimationFrame(drawVideoFrame);
    }
}

function renderRectangle(rectangle, className) {
    if (!overlay || !rectangle)
        return;

    const positioned = svgRect(rectangle);
    overlay.appendChild(element('rect', {
        class: className,
        x: positioned.x,
        y: positioned.y,
        width: positioned.width,
        height: positioned.height,
        rx: 0,
        ry: 0,
    }));
}

function renderOverlay() {
    if (!overlay)
        return;

    const bounds = wrapperRect();
    overlay.setAttribute('viewBox', `0 0 ${bounds.width} ${bounds.height}`);
    overlay.replaceChildren();
    overlay.classList.toggle('is-interactive', drawing || Boolean(region));
    overlay.classList.toggle('is-drawing', drawing);
    overlay.classList.toggle('is-editing', Boolean(editDrag));

    renderDetections();
    renderRectangle(region, 'region-outline region-outline--selected');
    renderRectangle(normaliseRectangle(draftStart, draftEnd), 'region-preview-rect');

    if (region) {
        for (const handle of regionHandles(region)) {
            const positioned = svgPoint(handle);
            overlay.appendChild(element('circle', {
                class: `region-edit-point region-edit-point--${handle.id}`,
                cx: positioned.x,
                cy: positioned.y,
                r: handle.id === 'move' ? 6 : 4,
                'data-region-handle': handle.id,
            }));
        }
    }
}

function renderButtons() {
    if (drawButton) {
        drawButton.hidden = !roiPipelineAvailable;
        drawButton.disabled = cropInProgress;
        drawButton.setAttribute('aria-pressed', drawing ? 'true' : 'false');
        drawButton.dataset.tooltip = drawing ? 'Drawing region' : region ? 'Replace region' : 'Draw crop region';
    }

    if (removeButton) {
        removeButton.hidden = !region;
        removeButton.disabled = cropInProgress;
        removeButton.dataset.tooltip = 'Delete region';
        removeButton.setAttribute('aria-label', 'Delete region');
    }

    if (applyCropButton) {
        applyCropButton.hidden = !roiPipelineAvailable || !region;
        applyCropButton.disabled = cropInProgress || !region;
        applyCropButton.dataset.tooltip = cropInProgress ? 'Restarting...' : 'Restart with region';
    }

    if (clearCropButton) {
        clearCropButton.hidden = !roiPipelineAvailable || !activeCrop;
        clearCropButton.disabled = cropInProgress;
        clearCropButton.setAttribute('aria-pressed', activeCrop ? 'true' : 'false');
    }

    document.body.classList.toggle('roi-crop-active', Boolean(activeCrop));
}

function render() {
    renderButtons();
    renderRegionCanvas();
    renderOverlay();
    window.dispatchEvent(new CustomEvent('regions-change'));
}

function releaseOverlayPointer(pointerId) {
    try {
        overlay?.releasePointerCapture?.(pointerId);
    } catch (error) {
        // The capture may already be gone if the browser cancelled the pointer.
    }
}

function startDrawing() {
    if (!roiPipelineAvailable || cropInProgress)
        return;

    region = null;
    editDrag = null;
    draftStart = null;
    draftEnd = null;
    drawing = true;
    drawingPointerId = null;
    render();
}

function stopDrawing() {
    drawing = false;
    drawingPointerId = null;
    editDrag = null;
    draftStart = null;
    draftEnd = null;
    render();
}

drawButton?.addEventListener('click', () => {
    startDrawing();
});

removeButton?.addEventListener('click', () => {
    region = null;
    stopDrawing();
});

async function updatePipelineCrop(roi) {
    if (!roiPipelineAvailable || cropInProgress)
        return;

    cropInProgress = true;
    render();

    try {
        const response = await fetch('/api/pipeline-roi', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ roi }),
        });
        const payload = await response.json().catch(() => ({}));
        if (!response.ok || payload.complete === false) {
            throw new Error(payload.message || 'Crop request failed.');
        }

        activeCrop = payload.roi || null;
        region = null;
        draftStart = null;
        draftEnd = null;
        drawing = false;
        drawingPointerId = null;
        editDrag = null;

        window.dispatchEvent(new CustomEvent('pipeline-restart', {
            detail: {
                available: true,
                requested: true,
                complete: true,
                message: payload.message || '',
                roi: activeCrop,
            },
        }));
    } catch (error) {
        console.warn('Pipeline crop request failed', error);
        applyCropButton?.setAttribute('data-tooltip', error.message || 'Crop failed');
    } finally {
        cropInProgress = false;
        render();
    }
}

applyCropButton?.addEventListener('click', () => {
    if (!region)
        return;
    updatePipelineCrop(region);
});

clearCropButton?.addEventListener('click', () => {
    updatePipelineCrop(null);
});

overlay?.addEventListener('pointerdown', (event) => {
    const target = event.target;
    if (
        region &&
        !cropInProgress &&
        target instanceof SVGCircleElement &&
        target.classList.contains('region-edit-point')
    ) {
        const point = eventPoint(event, true);
        if (!point)
            return;

        event.preventDefault();
        event.stopPropagation();
        drawing = false;
        drawingPointerId = null;
        editDrag = {
            pointerId: event.pointerId,
            handle: target.dataset.regionHandle,
            startPoint: point,
            startRegion: { ...region },
        };
        overlay.setPointerCapture?.(event.pointerId);
        render();
        return;
    }

    if (!drawing)
        return;

    const point = eventPoint(event);
    if (!point)
        return;

    event.preventDefault();
    draftStart = point;
    draftEnd = point;
    drawingPointerId = event.pointerId;
    overlay.setPointerCapture?.(event.pointerId);
    render();
});

overlay?.addEventListener('pointermove', (event) => {
    if (editDrag && editDrag.pointerId === event.pointerId) {
        const point = eventPoint(event, true);
        if (!point)
            return;

        region = resizedRegion(editDrag.startRegion, editDrag.handle, editDrag.startPoint, point);
        render();
        return;
    }

    if (!drawing || drawingPointerId !== event.pointerId || !draftStart)
        return;

    const point = eventPoint(event, true);
    if (!point)
        return;

    draftEnd = point;
    renderOverlay();
});

function finishPointerDrawing(event) {
    if (editDrag && editDrag.pointerId === event.pointerId) {
        editDrag = null;
        releaseOverlayPointer(event.pointerId);
        render();
        return;
    }

    if (!drawing || drawingPointerId !== event.pointerId)
        return;

    const drawnRegion = normaliseRectangle(draftStart, draftEnd);
    if (drawnRegion) {
        region = drawnRegion;
    }

    releaseOverlayPointer(event.pointerId);
    stopDrawing();
}

overlay?.addEventListener('pointerup', finishPointerDrawing);
overlay?.addEventListener('pointercancel', finishPointerDrawing);

window.addEventListener('keydown', (event) => {
    const activeTag = document.activeElement?.tagName?.toLowerCase();
    if (['input', 'textarea', 'select'].includes(activeTag) || document.activeElement?.isContentEditable)
        return;

    if (event.key === 'Escape' && (drawing || editDrag)) {
        event.preventDefault();
        stopDrawing();
        return;
    }

    if (['Backspace', 'Delete'].includes(event.key) && region) {
        event.preventDefault();
        region = null;
        stopDrawing();
    }
});

video?.addEventListener('loadedmetadata', render);
video?.addEventListener('loadeddata', render);
video?.addEventListener('playing', render);
window.addEventListener('resize', render);
window.addEventListener('video-layout-change', render);
window.addEventListener('ctrl-message', (event) => {
    if (!event.detail?.inference_output)
        return;

    latestInferenceOutput = event.detail.inference_output;
    renderOverlay();
});

async function loadCropStatus() {
    if (!roiPipelineAvailable)
        return;

    try {
        const response = await fetch('/api/supervisor/status', { cache: 'no-store' });
        if (!response.ok)
            return;
        const status = await response.json();
        activeCrop = status.roi || null;
        render();
    } catch (error) {
        console.warn('Pipeline crop status unavailable', error);
    }
}

window.addEventListener('pipeline-restart', (event) => {
    if (event.detail?.roi !== undefined) {
        activeCrop = event.detail.roi;
        render();
        return;
    }

    loadCropStatus();
});

loadCropStatus();
render();
