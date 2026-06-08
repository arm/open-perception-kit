const STORAGE_KEY = 'pek-layout:output-panels:v1';

const panels = {
    metrics: {
        button: document.getElementById('togglePerformanceOutput'),
        section: document.querySelector('[data-performance-metrics-section]'),
        label: 'Performance',
    },
    inference: {
        button: document.getElementById('toggleInferenceOutput'),
        section: document.querySelector('[data-inference-output-section]'),
        label: 'Inference',
    },
    debug: {
        button: document.getElementById('toggleDebugOutput'),
        section: document.querySelector('[data-debug-log-section]'),
        label: 'Debug',
    },
};

const panelKeys = Object.keys(panels);
let visibility = readVisibility();

function readVisibility() {
    try {
        const stored = JSON.parse(localStorage.getItem(STORAGE_KEY) || '{}');
        return Object.fromEntries(panelKeys.map((key) => [key, stored[key] !== false]));
    } catch {
        localStorage.removeItem(STORAGE_KEY);
        return Object.fromEntries(panelKeys.map((key) => [key, true]));
    }
}

function persistVisibility() {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(visibility));
}

function applyVisibility() {
    for (const key of panelKeys) {
        const panel = panels[key];
        const visible = visibility[key] !== false;

        if (panel.section) {
            panel.section.hidden = !visible;
        }

        if (panel.button) {
            panel.button.setAttribute('aria-pressed', visible ? 'true' : 'false');
            panel.button.setAttribute(
                'aria-label',
                `${visible ? 'Hide' : 'Show'} ${panel.label} output panel`
            );
        }
    }

    window.dispatchEvent(new CustomEvent('output-panels-change', {
        detail: {
            visiblePanels: panelKeys.filter((key) => visibility[key] !== false),
        },
    }));
}

for (const key of panelKeys) {
    panels[key].button?.addEventListener('click', () => {
        visibility = {
            ...visibility,
            [key]: visibility[key] === false,
        };
        persistVisibility();
        applyVisibility();
    });
}

applyVisibility();
