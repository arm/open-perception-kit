/**
 * Models Management Module
 * Handles fetching and displaying registered AI models
 */

import { ctrlSend } from "./ctrlws.js"

class ModelsManager {
    constructor() {
        this.container = document.getElementById('models-container');
        this.updateInterval = null;
        this._descriptionCache = new Map();
        this._tooltip = this._createTooltip();
    }

    _createTooltip() {
        const t = document.createElement('div');
        t.id = 'model-tooltip';
        t.style.position = 'fixed';
        t.style.pointerEvents = 'none';
        t.style.zIndex = '1000';
        t.style.display = 'none';
        t.className = 'model-tooltip';
        document.body.appendChild(t);
        return t;
    }

    async _fetchDescription(model) {
        // Return cached if available
        if (this._descriptionCache.has(model.element_name))
            return this._descriptionCache.get(model.element_name);
        try {
            const url = `/api/model-info?name=${encodeURIComponent(model.name)}`;
            const resp = await fetch(url, { cache: 'no-store' });
            if (resp.ok) {
                const j = await resp.json();
                const desc = j.description || j.opchain?.description || j.model?.description || '';
                this._descriptionCache.set(model.element_name, desc);
                return desc;
            }
        } catch (e) {
            // ignore
        }

        // Fallback to any inline description field from the model object
        const fallback = model.description || model.desc || '';
        this._descriptionCache.set(model.element_name, fallback);
        return fallback;
    }


    /**
     * Render the models list
     */
    render(models) {
        if (!this.container) return;

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

        // Sort models by name (which is the model_name) for consistent display
        const sortedModels = [...models].sort((a, b) =>
            a.name.localeCompare(b.name)
        );

        // Render each model
        sortedModels.forEach(model => {
            const modelItem = this.createModelItem(model);
            this.container.appendChild(modelItem);
        });
    }

    /**
     * Create a model item element
     */
    createModelItem(model) {
        const item = document.createElement('div');
        item.className = 'model-item';

        item.innerHTML = `
            <div class="model-info">
                <div class="model-name">${model.name}</div>
            </div>
            <label class="model-toggle-switch" aria-label="Toggle ${model.name}">
                <input type="checkbox" role="switch" ${model.active ? 'checked' : ''}>
                <span class="model-toggle-track" aria-hidden="true">
                    <span class="model-toggle-thumb"></span>
                </span>
            </label>
        `;

        const toggle = item.querySelector('input[type="checkbox"]');

        // Hover tooltip handling
        const nameEl = item.querySelector('.model-name');
        let hoverActive = false;
        nameEl.addEventListener('mouseenter', async (ev) => {
            hoverActive = true;
            const desc = await this._fetchDescription(model);
            this._tooltip.textContent = desc || 'No description available';
            this._tooltip.style.display = 'block';
            const rect = nameEl.getBoundingClientRect();
            // Place tooltip below the element to avoid overlapping the name
            const top = rect.bottom + 6; // 6px gap
            let left = rect.left;
            // ensure tooltip doesn't overflow viewport on initial placement
            const maxLeft = window.innerWidth - 12 - this._tooltip.offsetWidth;
            if (left > maxLeft) left = Math.max(8, maxLeft);
            this._tooltip.style.left = `${left}px`;
            this._tooltip.style.top = `${top}px`;
        });

        nameEl.addEventListener('mousemove', (ev) => {
            if (!hoverActive) return;
            // Only update horizontal position on mouse move so tooltip stays below the name
            let x = ev.clientX + 12;
            const maxLeft = window.innerWidth - 12 - this._tooltip.offsetWidth;
            if (x > maxLeft) x = Math.max(8, maxLeft);
            this._tooltip.style.left = `${x}px`;
        });

        nameEl.addEventListener('mouseleave', () => {
            hoverActive = false;
            this._tooltip.style.display = 'none';
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

        const previousActive = model.active;

        toggle.disabled = true;
        item.classList.add('model-pending');

        model.active = shouldBeActive;
        item.classList.toggle('model-active', shouldBeActive);

        console.log(`Toggle model: ${model.name} from ${previousActive} to ${shouldBeActive}`);

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

