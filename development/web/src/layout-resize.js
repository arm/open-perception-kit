const root = document.documentElement;
const sidePanel = document.getElementById('sidePanel');
const sideHandle = document.getElementById('sidePanelResizeHandle');
const dock = document.querySelector('.bottom-dock');
const dockPanels = document.querySelector('.bottom-dock-panels');
const dockHandle = document.getElementById('bottomDockResizeHandle');
const dockColumnHandles = [...document.querySelectorAll('[data-bottom-column-resize]')];
const dockSections = {
    metrics: document.querySelector('[data-performance-metrics-section]'),
    inference: document.querySelector('[data-inference-output-section]'),
    debug: document.querySelector('[data-debug-log-section]'),
};

const sidebarStorageKey = 'opk-layout:sidebar-width:v1';
const dockStorageKey = 'opk-layout:bottom-dock-height:v1';
const dockColumnStorageKey = 'opk-layout:bottom-dock-columns:v1';
const dockColumnKeys = ['inference', 'metrics', 'debug'];
const dockColumnVariables = {
    metrics: '--bottom-metrics-column',
    inference: '--bottom-inference-column',
    debug: '--bottom-debug-column',
};
let dockColumnFitFrame = null;
let dockColumnTransitionFitFrame = null;
let dockHeightAnimationFrame = null;

function px(value) {
    const parsed = Number.parseFloat(value);
    return Number.isFinite(parsed) ? parsed : null;
}

function clamp(value, min, max) {
    return Math.min(Math.max(value, min), max);
}

function sidebarLimits() {
    const viewport = window.innerWidth || 1200;
    return {
        min: 280,
        max: Math.max(340, Math.min(560, viewport * 0.48)),
    };
}

function dockLimits() {
    const viewport = window.innerHeight || 900;
    return {
        min: 116,
        max: Math.max(220, Math.min(560, viewport * 0.62)),
    };
}

function visibleDockColumnKeys() {
    return dockColumnKeys.filter((key) => {
        const section = dockSections[key];
        return section && !section.hidden && getComputedStyle(section).display !== 'none';
    });
}

function configureDockColumnHandles() {
    const visibleKeys = visibleDockColumnKeys();

    dockColumnKeys.forEach((key) => {
        if (dockSections[key]) {
            dockSections[key].style.gridArea = key;
        }
    });

    dockColumnHandles.forEach((handle, index) => {
        const leftKey = visibleKeys[index];
        const rightKey = visibleKeys[index + 1];
        const active = Boolean(leftKey && rightKey);

        handle.hidden = !active;
        if (active) {
            handle.dataset.bottomColumnResize = `${leftKey}-${rightKey}`;
            handle.style.gridArea = `resize${index}`;
        } else {
            handle.style.gridArea = '';
        }
    });

    if (!dockPanels)
        return;

    const columns = [];
    const areas = [];
    visibleKeys.forEach((key, index) => {
        columns.push(`var(${dockColumnVariables[key]})`);
        areas.push(key);
        if (index < visibleKeys.length - 1) {
            columns.push('10px');
            areas.push(`resize${index}`);
        }
    });

    dockPanels.style.gridTemplateColumns = columns.length ? columns.join(' ') : 'minmax(0, 1fr)';
    dockPanels.style.gridTemplateAreas = areas.length ? `"${areas.join(' ')}"` : 'none';
}

function visibleDockColumnHandles() {
    return dockColumnHandles.filter((handle) => !handle.hidden && getComputedStyle(handle).display !== 'none');
}

function dockColumnHandleWidth() {
    const handle = visibleDockColumnHandles()[0];
    const width = handle ? handle.getBoundingClientRect().width : 10;
    return width || 10;
}

function dockColumnAvailableWidth() {
    if (!dockPanels) return 0;

    const handleSpace = visibleDockColumnHandles().length * dockColumnHandleWidth();
    return Math.max(0, dockPanels.getBoundingClientRect().width - handleSpace);
}

function dockColumnMinimums(available) {
    const compact = available < 900;
    return {
        metrics: compact ? 180 : 220,
        inference: compact ? 220 : 260,
        debug: compact ? 220 : 260,
    };
}

function defaultDockColumnWidths() {
    const available = dockColumnAvailableWidth();
    const visibleKeys = visibleDockColumnKeys();
    const evenWidth = visibleKeys.length ? available / visibleKeys.length : 0;

    return Object.fromEntries(dockColumnKeys.map((key) => [
        key,
        visibleKeys.includes(key) ? evenWidth : 0,
    ]));
}

function readDockColumnWidths() {
    const visibleKeys = visibleDockColumnKeys();
    const measured = {};
    for (const key of dockColumnKeys) {
        measured[key] = dockSections[key]?.getBoundingClientRect().width || 0;
    }

    if (visibleKeys.length && visibleKeys.every((key) => measured[key] > 0))
        return measured;

    return defaultDockColumnWidths();
}

function normalizeDockColumnWidths(widths) {
    const available = dockColumnAvailableWidth();
    const visibleKeys = visibleDockColumnKeys();
    if (!available)
        return widths;
    if (!visibleKeys.length)
        return Object.fromEntries(dockColumnKeys.map((key) => [key, 0]));

    const minimums = dockColumnMinimums(available);
    const minTotal = visibleKeys.reduce((sum, key) => sum + minimums[key], 0);
    const usableMinimums = minTotal > available
        ? Object.fromEntries(visibleKeys.map((key) => [key, minimums[key] * available / minTotal]))
        : minimums;

    const next = Object.fromEntries(dockColumnKeys.map((key) => {
        if (!visibleKeys.includes(key)) {
            return [key, 0];
        }

        const requestedWidth = Number.isFinite(widths[key]) ? widths[key] : 0;
        return [key, Math.max(usableMinimums[key], requestedWidth)];
    }));

    let total = visibleKeys.reduce((sum, key) => sum + next[key], 0);
    if (total > available) {
        let excess = total - available;
        for (const key of ['inference', 'metrics', 'debug'].filter((item) => visibleKeys.includes(item))) {
            const shrinkable = Math.max(0, next[key] - usableMinimums[key]);
            const shrink = Math.min(shrinkable, excess);
            next[key] -= shrink;
            excess -= shrink;
            if (excess <= 0)
                break;
        }
    } else if (total < available) {
        const extra = available - total;
        const expandableTotal = visibleKeys.reduce((sum, key) => sum + next[key], 0) || 1;
        for (const key of visibleKeys) {
            next[key] += extra * next[key] / expandableTotal;
        }
    }

    return next;
}

function setDockColumnWidths(widths, persist = false) {
    configureDockColumnHandles();

    if (!dockPanels || !visibleDockColumnKeys().length)
        return;

    const next = normalizeDockColumnWidths(widths);
    root.style.setProperty('--bottom-metrics-column', `${Math.round(next.metrics)}px`);
    root.style.setProperty('--bottom-inference-column', `${Math.round(next.inference)}px`);
    root.style.setProperty('--bottom-debug-column', `${Math.round(next.debug)}px`);

    if (persist) {
        localStorage.setItem(dockColumnStorageKey, JSON.stringify({
            metrics: Math.round(next.metrics),
            inference: Math.round(next.inference),
            debug: Math.round(next.debug),
        }));
    }
}

function setEqualDockColumnWidths(persist = false) {
    configureDockColumnHandles();

    if (!dockPanels)
        return;

    const available = dockColumnAvailableWidth();
    const visibleKeys = visibleDockColumnKeys();
    const evenWidth = visibleKeys.length ? available / visibleKeys.length : 0;
    const next = Object.fromEntries(dockColumnKeys.map((key) => [
        key,
        visibleKeys.includes(key) ? evenWidth : 0,
    ]));

    root.style.setProperty('--bottom-metrics-column', `${Math.round(next.metrics)}px`);
    root.style.setProperty('--bottom-inference-column', `${Math.round(next.inference)}px`);
    root.style.setProperty('--bottom-debug-column', `${Math.round(next.debug)}px`);

    if (persist) {
        localStorage.setItem(dockColumnStorageKey, JSON.stringify({
            metrics: Math.round(next.metrics),
            inference: Math.round(next.inference),
            debug: Math.round(next.debug),
        }));
    }
}

function scheduleDockColumnFit(persist = false) {
    if (dockColumnFitFrame)
        cancelAnimationFrame(dockColumnFitFrame);

    dockColumnFitFrame = requestAnimationFrame(() => {
        dockColumnFitFrame = null;
        setDockColumnWidths(readDockColumnWidths(), persist);
    });
}

function fitDockColumnsDuringTransition(duration = 260) {
    const startedAt = performance.now();

    if (dockColumnTransitionFitFrame)
        cancelAnimationFrame(dockColumnTransitionFitFrame);

    const fit = () => {
        setDockColumnWidths(readDockColumnWidths());

        if (performance.now() - startedAt < duration) {
            dockColumnTransitionFitFrame = requestAnimationFrame(fit);
            return;
        }

        dockColumnTransitionFitFrame = null;
        scheduleDockColumnFit();
    };

    dockColumnTransitionFitFrame = requestAnimationFrame(fit);
}

function setSidebarWidth(width, persist = false) {
    const { min, max } = sidebarLimits();
    const nextWidth = clamp(width, min, max);
    root.style.setProperty('--sidebar-width', `${nextWidth}px`);
    scheduleDockColumnFit(persist);

    if (persist) {
        localStorage.setItem(sidebarStorageKey, String(Math.round(nextWidth)));
    }
}

function setDockHeight(height, persist = false) {
    const { min, max } = dockLimits();
    const nextHeight = clamp(height, min, max);
    root.style.setProperty('--bottom-dock-height', `${nextHeight}px`);

    if (persist) {
        localStorage.setItem(dockStorageKey, String(Math.round(nextHeight)));
    }
}

function dockTargetHeight() {
    const style = getComputedStyle(root);
    const expandedHeight = px(style.getPropertyValue('--bottom-dock-height')) || 244;
    const collapsedHeight = px(style.getPropertyValue('--bottom-dock-collapsed-height')) || 48;
    const outputsHidden = document.body.classList.contains('outputs-hidden');
    const outputsEmpty = document.body.classList.contains('output-panels-empty');

    if (outputsHidden)
        return 0;

    return outputsEmpty ? collapsedHeight : expandedHeight;
}

function easeOutCubic(progress) {
    return 1 - Math.pow(1 - progress, 3);
}

function animateBottomDockHeightChange(change) {
    const mainContent = document.querySelector('.main-content');
    if (!mainContent || !dock) {
        change();
        return;
    }

    if (dockHeightAnimationFrame)
        cancelAnimationFrame(dockHeightAnimationFrame);

    const startHeight = dock.getBoundingClientRect().height;
    mainContent.style.setProperty('--bottom-dock-current-height', `${startHeight}px`);

    change();

    const endHeight = dockTargetHeight();
    if (Math.abs(startHeight - endHeight) < 1 || window.matchMedia('(prefers-reduced-motion: reduce)').matches) {
        mainContent.style.setProperty('--bottom-dock-current-height', `${endHeight}px`);
        requestAnimationFrame(() => mainContent.style.removeProperty('--bottom-dock-current-height'));
        scheduleDockColumnFit();
        return;
    }

    const startedAt = performance.now();
    const duration = 220;

    const step = (now) => {
        const progress = clamp((now - startedAt) / duration, 0, 1);
        const nextHeight = startHeight + (endHeight - startHeight) * easeOutCubic(progress);
        mainContent.style.setProperty('--bottom-dock-current-height', `${nextHeight}px`);

        if (progress < 1) {
            dockHeightAnimationFrame = requestAnimationFrame(step);
            return;
        }

        dockHeightAnimationFrame = null;
        mainContent.style.setProperty('--bottom-dock-current-height', `${endHeight}px`);
        requestAnimationFrame(() => mainContent.style.removeProperty('--bottom-dock-current-height'));
        scheduleDockColumnFit();
    };

    dockHeightAnimationFrame = requestAnimationFrame(step);
}

window.animateBottomDockHeightChange = animateBottomDockHeightChange;

function restoreLayoutSizes() {
    const storedSidebarWidth = px(localStorage.getItem(sidebarStorageKey));
    if (storedSidebarWidth) {
        setSidebarWidth(storedSidebarWidth);
    }

    const storedDockHeight = px(localStorage.getItem(dockStorageKey));
    if (storedDockHeight) {
        setDockHeight(storedDockHeight);
    }

    try {
        const storedDockColumns = JSON.parse(localStorage.getItem(dockColumnStorageKey) || 'null');
        if (storedDockColumns && dockColumnKeys.every((key) => px(storedDockColumns[key]))) {
            setDockColumnWidths(storedDockColumns);
        }
    } catch {
        localStorage.removeItem(dockColumnStorageKey);
    }
}

function startSidebarResize(event) {
    if (event.button !== undefined && event.button !== 0) return;
    if (!sidePanel) return;

    event.preventDefault();
    const startX = event.clientX;
    const startWidth = sidePanel.getBoundingClientRect().width;

    document.body.classList.add('is-resizing-layout', 'is-resizing-sidebar');
    sideHandle?.setPointerCapture?.(event.pointerId);

    const move = (moveEvent) => {
        setSidebarWidth(startWidth + moveEvent.clientX - startX);
    };

    const stop = () => {
        document.body.classList.remove('is-resizing-layout', 'is-resizing-sidebar');
        const currentWidth = px(getComputedStyle(root).getPropertyValue('--sidebar-width')) || startWidth;
        setSidebarWidth(currentWidth, true);
        window.removeEventListener('pointermove', move);
        window.removeEventListener('pointerup', stop);
        window.removeEventListener('pointercancel', stop);
    };

    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', stop, { once: true });
    window.addEventListener('pointercancel', stop, { once: true });
}

function startDockResize(event) {
    if (event.button !== undefined && event.button !== 0) return;
    if (!dock) return;

    event.preventDefault();
    const startY = event.clientY;
    const startHeight = dock.getBoundingClientRect().height;

    document.body.classList.add('is-resizing-layout', 'is-resizing-bottom-dock');
    dockHandle?.setPointerCapture?.(event.pointerId);

    const move = (moveEvent) => {
        setDockHeight(startHeight + startY - moveEvent.clientY);
    };

    const stop = () => {
        document.body.classList.remove('is-resizing-layout', 'is-resizing-bottom-dock');
        const currentHeight = px(getComputedStyle(root).getPropertyValue('--bottom-dock-height')) || startHeight;
        setDockHeight(currentHeight, true);
        window.removeEventListener('pointermove', move);
        window.removeEventListener('pointerup', stop);
        window.removeEventListener('pointercancel', stop);
    };

    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', stop, { once: true });
    window.addEventListener('pointercancel', stop, { once: true });
}

function startDockColumnResize(event) {
    if (event.button !== undefined && event.button !== 0) return;
    if (!dock || !visibleDockColumnHandles().includes(event.currentTarget)) return;

    const resizeTarget = event.currentTarget.dataset.bottomColumnResize;
    const [leftKey, rightKey] = resizeTarget.split('-');
    if (!dockSections[leftKey] || !dockSections[rightKey]) return;

    event.preventDefault();
    const startX = event.clientX;
    const startWidths = readDockColumnWidths();
    const pairWidth = startWidths[leftKey] + startWidths[rightKey];
    const minimums = dockColumnMinimums(dockColumnAvailableWidth());
    const pairMinimumTotal = minimums[leftKey] + minimums[rightKey];
    const minScale = pairMinimumTotal > pairWidth ? pairWidth / pairMinimumTotal : 1;
    const minLeft = minimums[leftKey] * minScale;
    const minRight = minimums[rightKey] * minScale;

    document.body.classList.add('is-resizing-layout', 'is-resizing-bottom-column');
    event.currentTarget.setPointerCapture?.(event.pointerId);

    const move = (moveEvent) => {
        const delta = moveEvent.clientX - startX;
        const nextLeft = clamp(startWidths[leftKey] + delta, minLeft, pairWidth - minRight);
        const nextWidths = {
            ...startWidths,
            [leftKey]: nextLeft,
            [rightKey]: pairWidth - nextLeft,
        };
        setDockColumnWidths(nextWidths);
    };

    const stop = () => {
        document.body.classList.remove('is-resizing-layout', 'is-resizing-bottom-column');
        setDockColumnWidths(readDockColumnWidths(), true);
        window.removeEventListener('pointermove', move);
        window.removeEventListener('pointerup', stop);
        window.removeEventListener('pointercancel', stop);
    };

    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', stop, { once: true });
    window.addEventListener('pointercancel', stop, { once: true });
}

restoreLayoutSizes();
configureDockColumnHandles();
if (visibleDockColumnKeys().length > 1) {
    setEqualDockColumnWidths();
} else {
    scheduleDockColumnFit();
}

if (dockPanels && 'ResizeObserver' in window) {
    const dockPanelsResizeObserver = new ResizeObserver(() => {
        scheduleDockColumnFit();
    });
    dockPanelsResizeObserver.observe(dockPanels);
}

sideHandle?.addEventListener('pointerdown', startSidebarResize);
dockHandle?.addEventListener('pointerdown', startDockResize);
dockColumnHandles.forEach((handle) => handle.addEventListener('pointerdown', startDockColumnResize));

window.addEventListener('resize', () => {
    const currentSidebarWidth = px(getComputedStyle(root).getPropertyValue('--sidebar-width'));
    if (currentSidebarWidth) setSidebarWidth(currentSidebarWidth, true);

    const currentDockHeight = px(getComputedStyle(root).getPropertyValue('--bottom-dock-height'));
    if (currentDockHeight) setDockHeight(currentDockHeight, true);

    scheduleDockColumnFit(true);
});

window.addEventListener('output-panels-change', (event) => {
    configureDockColumnHandles();
    const visiblePanelCount = event.detail?.visiblePanels?.length || 0;
    if (visiblePanelCount > 1) {
        requestAnimationFrame(() => setEqualDockColumnWidths(true));
        return;
    }

    scheduleDockColumnFit(true);
});

window.addEventListener('video-layout-change', () => {
    configureDockColumnHandles();
    fitDockColumnsDuringTransition();
});

document.querySelector('.card-body')?.addEventListener('transitionend', (event) => {
    if (event.propertyName === 'grid-template-columns') {
        scheduleDockColumnFit();
    }
});

document.querySelector('.main-content')?.addEventListener('transitionend', (event) => {
    if (['--bottom-dock-current-height', 'grid-template-rows'].includes(event.propertyName)) {
        scheduleDockColumnFit();
    }
});
