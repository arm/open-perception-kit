/**
 * Models Management Module
 * Handles fetching and displaying registered AI models
 */

class ModelsManager {
    constructor() {
        this.container = document.getElementById('models-container');
        this.models = [];
        this.updateInterval = null;
    }

    /**
     * Initialize the models manager
     */
    init() {
        this.fetchModels();
        // Auto-refresh every 5 seconds
        this.updateInterval = setInterval(() => this.fetchModels(), 5000);
    }

    /**
     * Fetch models from the API
     */
    async fetchModels() {
        try {
            const response = await fetch('/models');
            
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }

            const data = await response.json();
            this.models = data.models || [];
            this.render();
        } catch (error) {
            console.error('Failed to fetch models:', error);
            this.renderError(error.message);
        }
    }

    /**
     * Render the models list
     */
    render() {
        if (!this.container) return;

        // Clear container
        this.container.innerHTML = '';

        // Handle empty state
        if (this.models.length === 0) {
            this.container.innerHTML = `
                <div class="models-empty">
                    No models registered yet
                </div>
            `;
            return;
        }

        // Sort models by model_name for consistent display
        const sortedModels = [...this.models].sort((a, b) => 
            a.model_name.localeCompare(b.model_name)
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
                <div class="model-name" title="${model.model_name}">${model.model_name}</div>
            </div>
            <button class="model-toggle-btn ${buttonClass}" data-model="${model.model_name}" data-active="${model.active}">
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
    handleToggle(model, button) {
        // Disable button to prevent double-clicks
        button.disabled = true;
        button.style.opacity = '0.5';
        button.style.cursor = 'not-allowed';

        console.log(`Toggle model: ${model.model_name} (currently ${model.active ? 'active' : 'inactive'})`);
        
        // TODO: Implement actual toggle API call
        // For now, just provide visual feedback
        setTimeout(() => {
            button.disabled = false;
            button.style.opacity = '';
            button.style.cursor = '';
        }, 1000);
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

// Initialize when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
    const modelsManager = new ModelsManager();
    modelsManager.init();
    
    // Store reference for debugging
    window.modelsManager = modelsManager;
});
