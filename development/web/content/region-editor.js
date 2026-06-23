const addButton = document.getElementById('addRegionBtn');
const removeButton = document.getElementById('removeRegionBtn');
const applyCropButton = document.getElementById('applyRoiCropBtn');
const clearCropButton = document.getElementById('clearRoiCropBtn');
const wrapper = document.querySelector('.video-wrapper');
const video = document.getElementById('video');
const canvas = document.getElementById('regionCanvas');
const overlay = document.getElementById('regionOverlay');

const CLOSE_DISTANCE_PX = 16;
const MIN_CROP_SIZE = 0.02;
const roiPipelineAvailable = Boolean(window.PEK_CONFIG?.supervised && window.PEK_CONFIG?.roiPipeline);
const regions = [];
let draftPoints = [];
let drawing = false;
let pointerPoint = null;
let selectedRegionIndex = -1;
let renderFrame = null;
let latestInferenceOutput = null;
let draggedPoint = null;
let suppressNextClick = false;
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

function eventPoint(event) {
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
        return null;
    }

    return {
        x: (x - rect.left) / rect.width,
        y: (y - rect.top) / rect.height,
    };
}

function pointDistance(a, b) {
    const start = svgPoint(a);
    const end = svgPoint(b);
    return Math.hypot(start.x - end.x, start.y - end.y);
}

function clamp(value, min = 0, max = 1) {
    return Math.min(Math.max(value, min), max);
}

function pointInRegion(point, region) {
    let inside = false;
    for (let index = 0, previous = region.length - 1; index < region.length; previous = index++) {
        const current = region[index];
        const last = region[previous];
        const intersects = (
            current.y > point.y
        ) !== (
            last.y > point.y
        ) && point.x < (last.x - current.x) * (point.y - current.y) / (last.y - current.y) + current.x;

        if (intersects) {
            inside = !inside;
        }
    }

    return inside;
}

function regionAtPoint(point) {
    for (let index = regions.length - 1; index >= 0; index -= 1) {
        if (pointInRegion(point, regions[index])) {
            return index;
        }
    }

    return -1;
}

function selectedRegion() {
    if (selectedRegionIndex >= 0)
        return regions[selectedRegionIndex] || null;
    if (regions.length === 1)
        return regions[0];
    return null;
}

function regionBounds(region) {
    if (!Array.isArray(region) || !region.length)
        return null;

    const xs = region.map((point) => clamp(point.x));
    const ys = region.map((point) => clamp(point.y));
    const left = Math.min(...xs);
    const right = Math.max(...xs);
    const top = Math.min(...ys);
    const bottom = Math.max(...ys);

    return {
        x: left,
        y: top,
        width: right - left,
        height: bottom - top,
    };
}

function selectedCropBounds() {
    const bounds = regionBounds(selectedRegion());
    if (!bounds)
        return null;
    if (bounds.width < MIN_CROP_SIZE || bounds.height < MIN_CROP_SIZE)
        return null;
    return bounds;
}

function pointsAttribute(points) {
    return points
        .map((point) => {
            const positioned = svgPoint(point);
            return `${positioned.x.toFixed(1)},${positioned.y.toFixed(1)}`;
        })
        .join(' ');
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

function detectionFullyInsideRegion(rect) {
    if (!regions.length)
        return true;

    return regions.some((region) => (
        rect.samplePoints.every((point) => pointInRegion(point, region))
    ));
}

function detectionAllowedByRegions(detection, dimensions = sourceDimensions()) {
    if (!regions.length)
        return true;

    const rect = detectionRect(detection, dimensions);
    if (!rect)
        return false;

    return detectionFullyInsideRegion(rect);
}

function filterInferenceOutput(output) {
    if (!regions.length || !Array.isArray(output?.layers))
        return output;

    const dimensions = sourceDimensions(output);
    return {
        ...output,
        layers: output.layers
            .map((layer) => {
                const detections = Array.isArray(layer.detections) ? layer.detections : [];
                const filteredDetections = detections.filter((detection) => (
                    detectionAllowedByRegions(detection, dimensions)
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
    hasRegions: () => regions.length > 0,
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
            if (!detectionAllowedByRegions(detection, dimensions))
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
    if (!video || !canvas || !regions.length) {
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
    context.save();
    context.setTransform(ratio, 0, 0, ratio, 0, 0);
    context.clearRect(0, 0, bounds.width, bounds.height);
    context.fillStyle = '#000000';
    context.fillRect(0, 0, bounds.width, bounds.height);

    try {
        context.filter = 'grayscale(1) brightness(0.72)';
        context.drawImage(video, rect.left, rect.top, rect.width, rect.height);
        context.filter = 'none';

        for (const region of regions) {
            context.save();
            region.forEach((point, index) => {
                const positioned = svgPoint(point);
                if (index === 0) {
                    context.beginPath();
                    context.moveTo(positioned.x, positioned.y);
                    return;
                }
                context.lineTo(positioned.x, positioned.y);
            });
            context.closePath();
            context.clip();
            context.drawImage(video, rect.left, rect.top, rect.width, rect.height);
            context.restore();
        }
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

    const hasRegions = regions.length > 0;
    video.classList.toggle('has-regions', hasRegions);
    canvas.classList.toggle('is-visible', hasRegions);

    if (!hasRegions) {
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

function renderOverlay() {
    if (!overlay)
        return;

    const bounds = wrapperRect();
    overlay.setAttribute('viewBox', `0 0 ${bounds.width} ${bounds.height}`);
    overlay.replaceChildren();
    overlay.classList.toggle('is-interactive', drawing || regions.length > 0);
    overlay.classList.toggle('is-drawing', drawing);

    renderDetections();

    regions.forEach((region, index) => {
        overlay.appendChild(element('polygon', {
            class: index === selectedRegionIndex
                ? 'region-outline region-outline--selected'
                : 'region-outline',
            points: pointsAttribute(region),
        }));
    });

    const selectedRegion = regions[selectedRegionIndex];
    if (!drawing && selectedRegion) {
        selectedRegion.forEach((point, index) => {
            const positioned = svgPoint(point);
            overlay.appendChild(element('circle', {
                class: 'region-edit-point',
                cx: positioned.x,
                cy: positioned.y,
                r: 5,
                'data-region-index': selectedRegionIndex,
                'data-point-index': index,
            }));
        });
    }

    const previewPoints = pointerPoint && draftPoints.length
        ? [...draftPoints, pointerPoint]
        : draftPoints;

    if (previewPoints.length > 1) {
        overlay.appendChild(element('polyline', {
            class: 'region-preview-line',
            points: pointsAttribute(previewPoints),
        }));
    }

    for (const [index, point] of draftPoints.entries()) {
        const positioned = svgPoint(point);
        overlay.appendChild(element('circle', {
            class: index === 0 ? 'region-point region-point--start' : 'region-point',
            cx: positioned.x,
            cy: positioned.y,
            r: index === 0 ? 6 : 4,
        }));
    }
}

function renderButtons() {
    if (removeButton) {
        removeButton.hidden = regions.length === 0;
        removeButton.dataset.tooltip = selectedRegionIndex >= 0
            ? 'Remove selected region'
            : 'Remove region';
        removeButton.setAttribute(
            'aria-label',
            selectedRegionIndex >= 0 ? 'Remove selected region' : 'Remove region'
        );
    }

    addButton?.setAttribute('aria-pressed', drawing ? 'true' : 'false');

    const cropBounds = selectedCropBounds();
    if (applyCropButton) {
        applyCropButton.hidden = !roiPipelineAvailable;
        applyCropButton.disabled = cropInProgress || !cropBounds;
        applyCropButton.dataset.tooltip = cropInProgress
            ? 'Applying crop...'
            : cropBounds
                ? 'Crop pipeline'
                : 'Select region first';
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

function startRegion() {
    drawing = true;
    selectedRegionIndex = -1;
    draftPoints = [];
    pointerPoint = null;
    render();
}

function stopDrawing() {
    drawing = false;
    draftPoints = [];
    pointerPoint = null;
    render();
}

function completeRegion() {
    if (draftPoints.length >= 3) {
        regions.push([...draftPoints]);
        selectedRegionIndex = regions.length - 1;
    }

    stopDrawing();
}

addButton?.addEventListener('click', () => {
    startRegion();
});

removeButton?.addEventListener('click', () => {
    if (!regions.length)
        return;

    const removeIndex = selectedRegionIndex >= 0 ? selectedRegionIndex : regions.length - 1;
    regions.splice(removeIndex, 1);
    selectedRegionIndex = -1;
    render();
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
        if (roi) {
            regions.length = 0;
            selectedRegionIndex = -1;
            draftPoints = [];
            pointerPoint = null;
        }

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
    const cropBounds = selectedCropBounds();
    if (!cropBounds)
        return;
    updatePipelineCrop(cropBounds);
});

clearCropButton?.addEventListener('click', () => {
    updatePipelineCrop(null);
});

overlay?.addEventListener('pointerdown', (event) => {
    const target = event.target;
    if (!(target instanceof SVGCircleElement) || !target.classList.contains('region-edit-point'))
        return;

    event.preventDefault();
    event.stopPropagation();
    draggedPoint = {
        pointerId: event.pointerId,
        regionIndex: Number(target.dataset.regionIndex),
        pointIndex: Number(target.dataset.pointIndex),
    };
    suppressNextClick = true;
    target.setPointerCapture?.(event.pointerId);
});

window.addEventListener('keydown', (event) => {
    if (!['Backspace', 'Delete'].includes(event.key) || selectedRegionIndex < 0)
        return;

    const activeTag = document.activeElement?.tagName?.toLowerCase();
    if (['input', 'textarea', 'select'].includes(activeTag) || document.activeElement?.isContentEditable)
        return;

    event.preventDefault();
    regions.splice(selectedRegionIndex, 1);
    selectedRegionIndex = -1;
    render();
});

overlay?.addEventListener('click', (event) => {
    if (suppressNextClick) {
        suppressNextClick = false;
        event.preventDefault();
        event.stopPropagation();
        return;
    }

    const point = eventPoint(event);

    if (drawing) {
        if (!point)
            return;

        if (
            draftPoints.length >= 3 &&
            pointDistance(point, draftPoints[0]) <= CLOSE_DISTANCE_PX
        ) {
            completeRegion();
            return;
        }

        draftPoints.push(point);
        pointerPoint = null;
        render();
        return;
    }

    selectedRegionIndex = point ? regionAtPoint(point) : -1;
    render();
});

overlay?.addEventListener('pointermove', (event) => {
    if (draggedPoint) {
        const point = eventPoint(event);
        const region = regions[draggedPoint.regionIndex];
        if (point && region?.[draggedPoint.pointIndex]) {
            region[draggedPoint.pointIndex] = {
                x: clamp(point.x),
                y: clamp(point.y),
            };
            selectedRegionIndex = draggedPoint.regionIndex;
            render();
        }
        return;
    }

    if (!drawing || !draftPoints.length)
        return;

    pointerPoint = eventPoint(event);
    renderOverlay();
});

overlay?.addEventListener('pointerleave', () => {
    if (draggedPoint)
        return;

    pointerPoint = null;
    renderOverlay();
});

function stopPointDrag() {
    if (!draggedPoint)
        return;

    draggedPoint = null;
    render();
}

window.addEventListener('pointerup', stopPointDrag);
window.addEventListener('pointercancel', stopPointDrag);

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
