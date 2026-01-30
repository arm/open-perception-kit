/**
 * Models Management Module
 * Handles fetching and displaying registered AI models
 */

import {ctrlSend} from "./ctrlws.js"

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

        // Sort models by model_name for consistent display
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
        item.dataset.modelName = model.model_name;
        item.dataset.elementName = model.element_name;

        const statusClass = model.active ? 'active' : 'inactive';
        const buttonClass = model.active ? 'disable' : 'enable';
        const buttonText = model.active ? 'Disable' : 'Enable';

        item.innerHTML = `
            <div class="model-info">
                <div class="model-status-dot ${statusClass}"></div>
                <div class="model-name" title="${model.name}">${model.name}</div>
            </div>
            <button class="model-toggle-btn ${buttonClass}" data-model="${model.name}" data-active="${model.active}">
                ${buttonText}
            </button>
        `;

        // Add click handler for toggle button
        const button = item.querySelector('.model-toggle-btn');
        button.addEventListener('click', () => this.handleToggle(model, button));

        return item;
    }

    /**
     * Handle toggle button click
     */
    async handleToggle(model, button) {
        // Disable button to prevent double-clicks
        button.disabled = true;
        button.style.opacity = '0.5';
        button.style.cursor = 'not-allowed';

        const newActiveState = !model.active;
        console.log(`Toggle model: ${model.model_name} from ${model.active} to ${newActiveState}`);
        
        try {

            ctrlSend({type: "model_toggle", name: model.element_name});

        } catch (error) {
            console.error('Error toggling model:', error);
            alert(`Error toggling model: ${error.message}`);
        } finally {
            // Re-enable button
            button.disabled = false;
            button.style.opacity = '';
            button.style.cursor = '';
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
    
