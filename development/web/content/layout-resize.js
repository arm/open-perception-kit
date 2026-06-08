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

const sidebarStorageKey = 'pek-layout:sidebar-width:v1';
const dockStorageKey = 'pek-layout:bottom-dock-height:v1';
const dockColumnStorageKey = 'pek-layout:bottom-dock-columns:v1';
const dockColumnKeys = ['inference', 'metrics', 'debug'];
const dockColumnVariables = {
    metrics: '--bottom-metrics-column',
    inference: '--bottom-inference-column',
    debug: '--bottom-debug-column',
};
const dockColumnWeights = {
    inference: 0.34,
    metrics: 0.3,
    debug: 0.36,
};
let dockColumnFitFrame = null;

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

    dockColumnHandles.forEach((handle, index) => {
        const leftKey = visibleKeys[index];
        const rightKey = visibleKeys[index + 1];
        const active = Boolean(leftKey && rightKey);

        handle.hidden = !active;
        if (active) {
            handle.dataset.bottomColumnResize = `${leftKey}-${rightKey}`;
        }
    });

    if (!dockPanels)
        return;

    const columns = [];
    visibleKeys.forEach((key, index) => {
        columns.push(`var(${dockColumnVariables[key]})`);
        if (index < visibleKeys.length - 1)
            columns.push('10px');
    });

    dockPanels.style.gridTemplateColumns = columns.length ? columns.join(' ') : 'minmax(0, 1fr)';
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
    const totalWeight = visibleKeys.reduce((sum, key) => sum + dockColumnWeights[key], 0) || 1;

    return Object.fromEntries(dockColumnKeys.map((key) => [
        key,
        visibleKeys.includes(key) ? available * dockColumnWeights[key] / totalWeight : 0,
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

    const next = Object.fromEntries(dockColumnKeys.map((key) => [
        key,
        visibleKeys.includes(key)
            ? Math.max(usableMinimums[key], Number.isFinite(widths[key]) ? widths[key] : 0)
            : 0,
    ]));

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
        next[visibleKeys[visibleKeys.length - 1]] += available - total;
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

function scheduleDockColumnFit(persist = false) {
    if (dockColumnFitFrame)
        cancelAnimationFrame(dockColumnFitFrame);

    dockColumnFitFrame = requestAnimationFrame(() => {
        dockColumnFitFrame = null;
        setDockColumnWidths(readDockColumnWidths(), persist);
    });
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
scheduleDockColumnFit();

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

window.addEventListener('output-panels-change', () => {
    configureDockColumnHandles();
    scheduleDockColumnFit(true);
});
