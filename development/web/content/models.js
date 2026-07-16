/**
 * Models Management Module
 * Handles fetching and displaying registered AI models.
 */

import { ctrlSend } from "./ctrlws.js"

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
        this._lastModelsSignature = '';
    }

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

        this.container.innerHTML = '';

        if (models.length === 0) {
            this.container.innerHTML = `
                <div class="models-empty">
                    No models registered yet
                </div>
            `;
            return;
        }

        orderModels(models).forEach((model) => {
            const modelItem = this.createModelItem(model);
            this.container.appendChild(modelItem);
        });
    }

    createModelItem(model) {
        const item = document.createElement('div');
        item.className = 'model-item';
        item.classList.toggle('model-active', Boolean(model.active));

        item.innerHTML = `
            <div class="model-info">
                <div class="model-name">${model.name}</div>
            </div>
            <div class="model-actions">
                <label class="model-toggle-switch" aria-label="Toggle ${model.name}">
                    <input type="checkbox" role="switch" ${model.active ? 'checked' : ''}>
                    <span class="model-toggle-track" aria-hidden="true">
                        <span class="model-toggle-thumb"></span>
                    </span>
                </label>
            </div>
        `;

        const toggle = item.querySelector('input[type="checkbox"]');
        toggle.addEventListener('change', () => {
            const shouldBeActive = toggle.checked;
            this.handleToggle(model, shouldBeActive, item, toggle);
        });

        return item;
    }

    async handleToggle(model, shouldBeActive, item, toggle) {
        if (model.active === shouldBeActive) {
            return;
        }

        const previousActive = model.active;

        toggle.disabled = true;
        item.classList.add('model-pending');

        model.active = shouldBeActive;
        item.classList.toggle('model-active', shouldBeActive);

        try {
            ctrlSend({ type: "model_toggle", name: model.element_name });
        } catch (error) {
            console.error('Error toggling model:', error);
            model.active = previousActive;
            toggle.checked = previousActive;
            item.classList.toggle('model-active', previousActive);
        } finally {
            toggle.disabled = false;
            item.classList.remove('model-pending');
        }
    }

    renderError(message) {
        if (!this.container) return;

        this.container.innerHTML = `
            <div class="models-error">
                <strong>Error loading models:</strong><br>
                ${message}
            </div>
        `;
    }

    destroy() {
        if (this.updateInterval) {
            clearInterval(this.updateInterval);
            this.updateInterval = null;
        }
    }
}

export const modelsManager = new ModelsManager();
