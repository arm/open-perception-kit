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
        this._infoPanel = this._createInfoPanel();
        this._activeInfoButton = null;

        document.addEventListener('click', (event) => {
            if (!this._infoPanel || this._infoPanel.style.display === 'none') {
                return;
            }

            if (this._infoPanel.contains(event.target) || event.target.closest('.model-info-button')) {
                return;
            }

            this._hideInfoPanel();
        });
    }

    _createInfoPanel() {
        const panel = document.createElement('div');
        panel.id = 'model-info-panel';
        panel.style.position = 'fixed';
        panel.style.zIndex = '1000';
        panel.style.display = 'none';
        panel.className = 'model-info-panel';
        document.body.appendChild(panel);
        return panel;
    }

    _hideInfoPanel() {
        if (!this._infoPanel) return;

        this._infoPanel.style.display = 'none';
        if (this._activeInfoButton) {
            this._activeInfoButton.setAttribute('aria-expanded', 'false');
        }
        this._activeInfoButton = null;
    }

    _showInfoPanel(buttonEl, text) {
        this._infoPanel.textContent = text || 'No description available';
        this._infoPanel.style.display = 'block';

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
            <div class="model-actions">
                <label class="model-toggle-switch" aria-label="Toggle ${model.name}">
                    <input type="checkbox" role="switch" ${model.active ? 'checked' : ''}>
                    <span class="model-toggle-track" aria-hidden="true">
                        <span class="model-toggle-thumb"></span>
                    </span>
                </label>
                <button class="model-info-button" type="button" aria-label="Show information for ${model.name}" aria-expanded="false">i</button>
            </div>
        `;

        const toggle = item.querySelector('input[type="checkbox"]');
        const infoButton = item.querySelector('.model-info-button');

        infoButton.addEventListener('click', async (event) => {
            event.stopPropagation();

            if (this._activeInfoButton === infoButton && this._infoPanel.style.display !== 'none') {
                this._hideInfoPanel();
                return;
            }

            if (this._activeInfoButton) {
                this._activeInfoButton.setAttribute('aria-expanded', 'false');
            }

            const desc = await this._fetchDescription(model);
            this._activeInfoButton = infoButton;
            infoButton.setAttribute('aria-expanded', 'true');
            this._showInfoPanel(infoButton, desc);
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

