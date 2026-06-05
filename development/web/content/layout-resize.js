const root = document.documentElement;
const sidePanel = document.getElementById('sidePanel');
const sideHandle = document.getElementById('sidePanelResizeHandle');
const dock = document.querySelector('.bottom-dock');
const dockHandle = document.getElementById('bottomDockResizeHandle');

const sidebarStorageKey = 'pek-layout:sidebar-width:v1';
const dockStorageKey = 'pek-layout:bottom-dock-height:v1';

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

function setSidebarWidth(width, persist = false) {
    const { min, max } = sidebarLimits();
    const nextWidth = clamp(width, min, max);
    root.style.setProperty('--sidebar-width', `${nextWidth}px`);

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
}

function startSidebarResize(event) {
    if (event.button !== undefined && event.button !== 0) return;
    if (!sidePanel || document.body.classList.contains('side-panel-collapsed')) return;

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

restoreLayoutSizes();

sideHandle?.addEventListener('pointerdown', startSidebarResize);
dockHandle?.addEventListener('pointerdown', startDockResize);

window.addEventListener('resize', () => {
    const currentSidebarWidth = px(getComputedStyle(root).getPropertyValue('--sidebar-width'));
    if (currentSidebarWidth) setSidebarWidth(currentSidebarWidth, true);

    const currentDockHeight = px(getComputedStyle(root).getPropertyValue('--bottom-dock-height'));
    if (currentDockHeight) setDockHeight(currentDockHeight, true);
});
