/**
 * Models Management Module
 * Handles fetching and displaying registered AI models
 */

import { ctrlSend } from "./ctrlws.js?v=disabled-toggle-tooltip-20260608"

const PREFERRED_MODEL_ORDER = [
    'YoloV11',
    'OsnetX025Reid',
    'Ultraface',
    'CameraContact',
    'GazeDetection',
];

function modelKey(modelOrName) {
    return String(modelOrName?.name || modelOrName || '').toLowerCase();
}

function modelStateKey(model) {
    return modelKey(model?.element_name || model?.name);
}

function dependencyTokensForModel(modelName) {
    const lowered = modelKey(modelName);

    if (lowered.includes('cameracontact') || lowered.includes('gazedetection')) {
        return ['ultraface'];
    }

    if (lowered.includes('osnetx025reid')) {
        return ['yolov11'];
    }

    return [];
}

function modelNameMatchesDependency(name, token) {
    const lowered = modelKey(name);

    if (lowered === token) {
        return true;
    }

    if (!lowered.startsWith(token) || lowered.length <= token.length) {
        return false;
    }

    const separator = lowered[token.length];
    return separator === ' ' || separator === '-' || separator === '_';
}

function buildForcedDependencyState(models) {
    const forcedBy = new Map();

    models.forEach((model) => {
        if (!model.active) {
            return;
        }

        dependencyTokensForModel(model.name).forEach((dependencyToken) => {
            models.forEach((candidate) => {
                if (
                    modelStateKey(candidate) !== modelStateKey(model) &&
                    modelNameMatchesDependency(candidate.name, dependencyToken)
                ) {
                    const key = modelStateKey(candidate);
                    const children = forcedBy.get(key) || [];
                    children.push(model.name);
                    forcedBy.set(key, children);
                }
            });
        });
    });

    return forcedBy;
}

function orderModels(models) {
    const preferred = new Map(PREFERRED_MODEL_ORDER.map((name, index) => [modelKey(name), index]));

    return [...models].sort((a, b) => {
        const aOrder = preferred.get(modelKey(a));
        const bOrder = preferred.get(modelKey(b));

        if (aOrder !== undefined || bOrder !== undefined) {
            return (aOrder ?? Number.MAX_SAFE_INTEGER) - (bOrder ?? Number.MAX_SAFE_INTEGER);
        }

        return String(a.name || '').localeCompare(String(b.name || ''));
    });
}

class ModelsManager {
    constructor() {
        this.container = document.getElementById('models-container');
        this.updateInterval = null;
        this._descriptionCache = new Map();
        this._modelDetailsCache = new Map();
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
        const details = await this._fetchModelDetails(model);
        const desc = details?.description || details?.opchain?.description || details?.model?.description || '';
        if (desc)
            return desc;

        const fallback = model.description || model.desc || '';
        this._descriptionCache.set(this._modelCacheKey(model), fallback);
        return fallback;
    }

    _modelCacheKey(model) {
        return `${model.name}::${model.element_name || ''}`;
    }

    async _fetchModelDetails(model) {
        const cacheKey = this._modelCacheKey(model);
        if (this._modelDetailsCache.has(cacheKey))
            return this._modelDetailsCache.get(cacheKey);
        if (this._descriptionCache.has(cacheKey))
            return { description: this._descriptionCache.get(cacheKey) };

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
                if (!j.error) {
                    this._modelDetailsCache.set(cacheKey, j);
                    const desc = j.description || j.opchain?.description || j.model?.description || '';
                    if (desc)
                        this._descriptionCache.set(cacheKey, desc);
                    return j;
                }
            }
        } catch (e) {
            // ignore
        }

        const fallback = { description: model.description || model.desc || '' };
        this._modelDetailsCache.set(cacheKey, fallback);
        return fallback;
    }

    _tensorOptionsForDetails(details) {
        const options = details?.tensorInputOptions || details?.tensorSizeOptions || details?.inputTensorOptions;
        if (!Array.isArray(options))
            return [];

        return options.filter((option) => option && option.value && option.label);
    }

    async _hydrateTensorSelect(model, item) {
        const slot = item.querySelector('.model-tensor-slot');
        if (!slot)
            return;

        const details = await this._fetchModelDetails(model);
        const options = this._tensorOptionsForDetails(details);
        if (!options.length) {
            slot.hidden = true;
            slot.replaceChildren();
            return;
        }

        const select = document.createElement('select');
        select.className = 'model-tensor-select';
        select.setAttribute('aria-label', `${model.name} tensor size`);
        options.forEach((option) => {
            const optionElement = document.createElement('option');
            optionElement.value = option.value;
            optionElement.textContent = option.label;
            select.append(optionElement);
        });

        slot.replaceChildren(select);
        slot.hidden = false;
    }


    /**
     * Render the models list
     */
    render(models) {
        if (!this.container) return;

        const forcedBy = buildForcedDependencyState(models);
        const nextSignature = JSON.stringify(models.map((model) => ({
            active: Boolean(model.active),
            element_name: model.element_name || '',
            forcedBy: forcedBy.get(modelStateKey(model)) || [],
            name: model.name || '',
            tensorOptionsKnown: this._modelDetailsCache.has(this._modelCacheKey(model)),
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

        // Render each model
        orderModels(models).forEach((model) => {
            const modelItem = this.createModelItem(model, forcedBy.get(modelStateKey(model)) || []);
            this.container.appendChild(modelItem);
        });
    }

    /**
     * Create a model item element
     */
    createModelItem(model, forcedBy = []) {
        const item = document.createElement('div');
        item.className = 'model-item';
        const isForced = forcedBy.length > 0;
        const displayActive = Boolean(model.active || isForced);
        if (isForced) {
            item.classList.add('model-item--dependency-forced');
        }

        const toggleLabel = isForced
            ? `${model.name} is required by ${forcedBy.join(', ')}`
            : `Toggle ${model.name}`;

        const toggleMarkup = `
            <label class="model-toggle-switch" aria-label="${toggleLabel}" ${isForced ? 'data-tooltip="Cannot be disabled as this model is required by an enabled model."' : ''}>
                <input type="checkbox" role="switch" ${displayActive ? 'checked' : ''} ${isForced ? 'disabled aria-disabled="true"' : ''}>
                <span class="model-toggle-track" aria-hidden="true">
                    <span class="model-toggle-thumb"></span>
                </span>
            </label>
        `;

        const infoMarkup = `
            <button class="model-info-button" type="button" aria-label="Show information for ${model.name}" aria-expanded="false">i</button>
        `;

        item.innerHTML = `
            <div class="model-info">
                <div class="model-name">${model.name}</div>
            </div>
            <div class="model-actions">
                ${toggleMarkup}
                <span class="model-tensor-slot" hidden></span>
                ${infoMarkup}
            </div>
        `;

        const toggle = item.querySelector('input[type="checkbox"]');
        const infoButton = item.querySelector('.model-info-button');
        const infoKey = modelKey(model);
        this._hydrateTensorSelect(model, item);

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
            if (toggle.disabled) {
                return;
            }

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
            ctrlSend({ type: "model_toggle", name: model.element_name, active: shouldBeActive });

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
