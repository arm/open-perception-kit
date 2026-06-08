/**
 * Models Management Module
 * Handles fetching and displaying registered AI models
 */

import { ctrlSend } from "./ctrlws.js?v=chrome-freeze-pause-20260605"

const MODEL_DEPENDENCY_GROUPS = [
    {
        parent: 'YoloV11',
        children: ['OsnetX025Reid'],
    },
    {
        parent: 'Ultraface',
        children: ['CameraContact', 'GazeDetection'],
    },
];

function modelKey(modelOrName) {
    return String(modelOrName?.name || modelOrName || '').toLowerCase();
}

class ModelsManager {
    constructor() {
        this.container = document.getElementById('models-container');
        this.updateInterval = null;
        this._descriptionCache = new Map();
        this._infoPanel = this._createInfoPanel();
        this._activeInfoButton = null;
        this._activeInfoKey = null;
        this._infoRequestId = 0;
        this._lastModelsSignature = '';
    }

    _createInfoPanel() {
        const panel = document.createElement('div');
        panel.id = 'model-info-panel';
        panel.style.position = 'fixed';
        panel.style.zIndex = '1000';
        panel.style.display = 'none';
        panel.className = 'model-info-panel';
        panel.setAttribute('aria-hidden', 'true');
        document.body.appendChild(panel);
        return panel;
    }

    _hideInfoPanel() {
        if (!this._infoPanel) return;

        this._infoPanel.style.display = 'none';
        this._infoPanel.setAttribute('aria-hidden', 'true');
        if (this._activeInfoButton) {
            this._activeInfoButton.setAttribute('aria-expanded', 'false');
        }
        this._activeInfoButton = null;
        this._activeInfoKey = null;
        this._infoRequestId += 1;
    }

    _showInfoPanel(buttonEl, text) {
        this._infoPanel.textContent = text || 'No description available';
        this._infoPanel.style.display = 'block';
        this._infoPanel.setAttribute('aria-hidden', 'false');

        const rect = buttonEl.getBoundingClientRect();
        const panelRect = this._infoPanel.getBoundingClientRect();
        const gap = 10;

        let left = rect.right + gap;
        let top = rect.top + (rect.height - panelRect.height) / 2;

        if (left + panelRect.width > window.innerWidth - 12) {
            left = Math.max(8, rect.left - panelRect.width - gap);
        }

        if (top < 8) {
            top = 8;
        }
        if (top + panelRect.height > window.innerHeight - 8) {
            top = Math.max(8, window.innerHeight - panelRect.height - 8);
        }

        this._infoPanel.style.left = `${left}px`;
        this._infoPanel.style.top = `${top}px`;
    }

    async _fetchDescription(model) {
        // Return cached if available
        const descriptionKey = `${model.name}::${model.element_name || ''}`;
        if (this._descriptionCache.has(descriptionKey))
            return this._descriptionCache.get(descriptionKey);

        const lookupCandidates = [model.name, model.element_name].filter(
            (value, index, array) => value && array.indexOf(value) === index
        );

        try {
            for (const lookupName of lookupCandidates) {
                const url = `/api/model-info?name=${encodeURIComponent(lookupName)}`;
                const resp = await fetch(url, { cache: 'no-store' });
                if (!resp.ok) {
                    continue;
                }

                const j = await resp.json();
                const desc = j.description || j.opchain?.description || j.model?.description || '';
                if (desc) {
                    this._descriptionCache.set(descriptionKey, desc);
                    return desc;
                }
            }
        } catch (e) {
            // ignore
        }

        // Fallback to any inline description field from the model object
        const fallback = model.description || model.desc || '';
        this._descriptionCache.set(descriptionKey, fallback);
        return fallback;
    }


    /**
     * Render the models list
     */
    render(models) {
        if (!this.container) return;

        const nextSignature = JSON.stringify(models.map((model) => ({
            active: Boolean(model.active),
            element_name: model.element_name || '',
            name: model.name || '',
        })));
        if (nextSignature === this._lastModelsSignature) {
            return;
        }
        this._lastModelsSignature = nextSignature;
        this._hideInfoPanel();

        // Clear container
        this.container.innerHTML = '';

        // Handle empty state
        if (models.length === 0) {
            this.container.innerHTML = `
                <div class="models-empty">
                    No models registered yet
                </div>
            `;
            return;
        }

        const modelByName = new Map(models.map((model) => [modelKey(model), model]));
        const rendered = new Set();
        const orderedModels = [];

        MODEL_DEPENDENCY_GROUPS.forEach((group) => {
            const parent = modelByName.get(modelKey(group.parent));
            if (!parent) return;

            orderedModels.push({
                model: parent,
                relation: {
                    type: 'parent',
                    childCount: group.children
                        .map((childName) => modelByName.get(modelKey(childName)))
                        .filter(Boolean).length,
                },
            });
            rendered.add(modelKey(parent));

            const children = group.children
                .map((childName) => modelByName.get(modelKey(childName)))
                .filter(Boolean);

            children.forEach((child, index) => {
                orderedModels.push({
                    model: child,
                    relation: {
                        type: 'child',
                        first: index === 0,
                        last: index === children.length - 1,
                    },
                });
                rendered.add(modelKey(child));
            });
        });

        [...models]
            .filter((model) => !rendered.has(modelKey(model)))
            .sort((a, b) => a.name.localeCompare(b.name))
            .forEach((model) => {
                orderedModels.push({
                    model,
                    relation: { type: 'standalone' },
                });
            });

        // Render each model
        orderedModels.forEach(({ model, relation }) => {
            const modelItem = this.createModelItem(model, relation);
            this.container.appendChild(modelItem);
        });
    }

    /**
     * Create a model item element
     */
    createModelItem(model, relation = { type: 'standalone' }) {
        const item = document.createElement('div');
        item.className = `model-item model-item--${relation.type}`;
        if (relation.first) item.classList.add('model-item--child-first');
        if (relation.last) item.classList.add('model-item--child-last');
        if (relation.type === 'parent' && relation.childCount) {
            item.classList.add(`model-item--parent-${relation.childCount}-children`);
        }

        const toggleMarkup = `
            <label class="model-toggle-switch" aria-label="Toggle ${model.name}">
                <input type="checkbox" role="switch" ${model.active ? 'checked' : ''}>
                <span class="model-toggle-track" aria-hidden="true">
                    <span class="model-toggle-thumb"></span>
                </span>
            </label>
        `;

        const infoMarkup = `
            <button class="model-info-button" type="button" aria-label="Show information for ${model.name}" aria-expanded="false">i</button>
        `;

        if (relation.type === 'parent') {
            item.innerHTML = `
                <div class="model-parent-toggle">
                    ${toggleMarkup}
                </div>
                <div class="model-info">
                    <div class="model-name">${model.name}</div>
                </div>
                <div class="model-actions">
                    ${infoMarkup}
                </div>
            `;
        } else {
            item.innerHTML = `
            <div class="model-info">
                <div class="model-name">${model.name}</div>
            </div>
            <div class="model-actions">
                ${toggleMarkup}
                ${infoMarkup}
            </div>
        `;
        }

        const toggle = item.querySelector('input[type="checkbox"]');
        const infoButton = item.querySelector('.model-info-button');
        const infoKey = modelKey(model);

        const showInfo = async () => {
            if (this._activeInfoKey === infoKey && this._infoPanel.style.display !== 'none') {
                this._showInfoPanel(infoButton, this._infoPanel.textContent);
                return;
            }

            if (this._activeInfoButton) {
                this._activeInfoButton.setAttribute('aria-expanded', 'false');
            }

            const requestId = ++this._infoRequestId;
            this._activeInfoButton = infoButton;
            this._activeInfoKey = infoKey;
            infoButton.setAttribute('aria-expanded', 'true');

            const desc = await this._fetchDescription(model);
            if (requestId !== this._infoRequestId || this._activeInfoKey !== infoKey) {
                return;
            }

            this._activeInfoButton = infoButton;
            infoButton.setAttribute('aria-expanded', 'true');
            this._showInfoPanel(infoButton, desc);
        };

        const hideInfo = () => {
            if (this._activeInfoKey === infoKey) {
                this._hideInfoPanel();
            }
        };

        infoButton.addEventListener('pointerenter', showInfo);
        infoButton.addEventListener('mouseenter', showInfo);
        infoButton.addEventListener('pointerleave', hideInfo);
        infoButton.addEventListener('mouseleave', hideInfo);

        infoButton.addEventListener('click', (event) => {
            event.preventDefault();
            event.stopPropagation();
        });

        toggle.addEventListener('change', () => {
            const shouldBeActive = toggle.checked;
            this.handleToggle(model, shouldBeActive, item, toggle);
        });

        return item;
    }

    /**
     * Handle radio selection changes
     */
    async handleToggle(model, shouldBeActive, item, toggle) {
        if (model.active === shouldBeActive) {
            return;
        }

        toggle.disabled = true;
        item.classList.add('model-pending');

        model.active = shouldBeActive;
        item.classList.toggle('model-active', shouldBeActive);

        try {
            ctrlSend({ type: "model_toggle", name: model.element_name });

        } catch (error) {
            console.error('Error toggling model:', error);
            alert(`Error toggling model: ${error.message}`);
        } finally {
            toggle.disabled = false;
            item.classList.remove('model-pending');
        }
    }

    /**
     * Render error state
     */
    renderError(message) {
        if (!this.container) return;

        this.container.innerHTML = `
            <div class="models-error">
                <strong>Error loading models:</strong><br>
                ${message}
            </div>
        `;
    }

    /**
     * Stop auto-refresh
     */
    destroy() {
        if (this.updateInterval) {
            clearInterval(this.updateInterval);
            this.updateInterval = null;
        }
    }
}

export const modelsManager = new ModelsManager();
