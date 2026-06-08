const STORAGE_KEY = 'pek-layout:output-panels:v1';

const panels = {
    inference: {
        button: document.getElementById('toggleInferenceOutput'),
        section: document.querySelector('[data-inference-output-section]'),
        label: 'Inference',
    },
    metrics: {
        button: document.getElementById('togglePerformanceOutput'),
        section: document.querySelector('[data-performance-metrics-section]'),
        label: 'Performance',
    },
    debug: {
        button: document.getElementById('toggleDebugOutput'),
        section: document.querySelector('[data-debug-log-section]'),
        label: 'Debug',
    },
};

const panelKeys = Object.keys(panels);
const DEFAULT_VISIBILITY = {
    inference: true,
    metrics: false,
    debug: false,
};
let visibility = readVisibility();

function readVisibility() {
    try {
        const raw = localStorage.getItem(STORAGE_KEY);
        if (!raw) {
            return { ...DEFAULT_VISIBILITY };
        }

        const stored = JSON.parse(raw);
        return Object.fromEntries(
            panelKeys.map((key) => [
                key,
                typeof stored[key] === 'boolean' ? stored[key] : DEFAULT_VISIBILITY[key],
            ]),
        );
    } catch {
        localStorage.removeItem(STORAGE_KEY);
        return { ...DEFAULT_VISIBILITY };
    }
}

function persistVisibility() {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(visibility));
}

function applyVisibility() {
    const visiblePanels = panelKeys.filter((key) => visibility[key] !== false);

    for (const key of panelKeys) {
        const panel = panels[key];
        const visible = visibility[key] !== false;

        if (panel.section) {
            panel.section.hidden = !visible;
        }

        if (panel.button) {
            const icon = panel.button.querySelector('i');
            panel.button.setAttribute('aria-pressed', visible ? 'true' : 'false');
            panel.button.setAttribute(
                'aria-label',
                `${visible ? 'Hide' : 'Show'} ${panel.label} output panel`
            );

            if (icon) {
                icon.className = visible
                    ? 'fa-solid fa-eye'
                    : 'fa-solid fa-eye-slash';
            }
        }
    }

    document.body.classList.toggle('output-panels-empty', visiblePanels.length === 0);

    window.dispatchEvent(new CustomEvent('output-panels-change', {
        detail: {
            visiblePanels,
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
