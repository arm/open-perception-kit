/**
 * Models Management Module
 * Handles fetching and displaying registered AI models
 */

import { ctrlSend } from "./ctrlws.js"

class ModelsManager {
    constructor() {
        this.container = document.getElementById('models-container');
        this.updateInterval = null;
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
                <div class="model-name" title="${model.name}">${model.name}</div>
            </div>
            <label class="model-toggle-switch" aria-label="Toggle ${model.name}">
                <input type="checkbox" role="switch" ${model.active ? 'checked' : ''}>
                <span class="model-toggle-track" aria-hidden="true">
                    <span class="model-toggle-thumb"></span>
                </span>
            </label>
        `;

        const toggle = item.querySelector('input[type="checkbox"]');

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

