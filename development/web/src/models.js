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

function readableText(value) {
    return String(value ?? '').trim();
}

function contentTypes(value) {
    return Array.isArray(value) ? value.map(readableText).filter(Boolean) : [];
}

function dependencyProviderTasks(model, models) {
    const required = new Set(contentTypes(model.requiredContentTypes));
    return [...new Set(models
        .filter((candidate) => candidate !== model &&
            contentTypes(candidate.providedContentTypes).some((type) => required.has(type)))
        .map((candidate) => readableText(candidate.task) || readableText(candidate.name))
        .filter(Boolean))];
}

function resolveModelPresentation(model) {
    const rawName = readableText(model.name) || 'Unknown model';
    const displayName = readableText(model.displayName) || rawName;
    const task = readableText(model.task);
    const runtime = readableText(model.runtime);
    const primaryLabel = task || displayName;
    let secondaryLabel = '';

    if (task) {
        secondaryLabel = runtime ? `${displayName} (${runtime})` : displayName;
    } else if (runtime) {
        secondaryLabel = runtime;
    }

    const fullLabel = secondaryLabel ? `${primaryLabel} - ${secondaryLabel}` : primaryLabel;
    return {primaryLabel, secondaryLabel, fullLabel};
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
            displayName: model.displayName || '',
            task: model.task || '',
            runtime: model.runtime || '',
            providedContentTypes: contentTypes(model.providedContentTypes),
            requiredContentTypes: contentTypes(model.requiredContentTypes),
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

        const orderedModels = orderModels(models);
        orderedModels.forEach((model) => {
            const modelItem = this.createModelItem(model, orderedModels);
            this.container.appendChild(modelItem);
        });
    }

    createModelItem(model, models) {
        const item = document.createElement('div');
        item.className = 'model-item';
        item.setAttribute('data-model-name', model.name || '');
        item.setAttribute('data-model-element-name', model.element_name || '');
        item.classList.toggle('model-active', Boolean(model.active));

        const presentation = resolveModelPresentation(model);
        const modelInfo = document.createElement('div');
        modelInfo.className = 'model-info';

        const modelCopy = document.createElement('div');
        modelCopy.className = 'model-copy';
        modelCopy.title = presentation.fullLabel;

        const modelTask = document.createElement('div');
        modelTask.className = 'model-task';
        modelTask.textContent = presentation.primaryLabel;
        modelCopy.appendChild(modelTask);

        if (presentation.secondaryLabel) {
            const modelDetails = document.createElement('div');
            modelDetails.className = 'model-details';
            modelDetails.textContent = presentation.secondaryLabel;
            modelCopy.appendChild(modelDetails);
        }

        const requiredContentTypes = contentTypes(model.requiredContentTypes);
        modelInfo.appendChild(modelCopy);

        const modelActions = document.createElement('div');
        modelActions.className = 'model-actions';

        if (requiredContentTypes.length > 0) {
            const providerTasks = dependencyProviderTasks(model, models);
            const accessibleProviders = providerTasks.length > 0
                ? providerTasks.join(', ')
                : 'No provider registered';
            const dependencyInfo = document.createElement('div');
            dependencyInfo.className = 'model-dependency-info';
            dependencyInfo.setAttribute('tabindex', '0');
            dependencyInfo.setAttribute('aria-label', `Depends on: ${accessibleProviders}`);

            const dependencyIcon = document.createElement('div');
            dependencyIcon.className = 'model-dependency-icon';
            dependencyIcon.textContent = 'i';
            dependencyIcon.setAttribute('aria-hidden', 'true');

            const dependencyPopup = document.createElement('div');
            dependencyPopup.className = 'model-dependency-popup';
            dependencyPopup.setAttribute('role', 'tooltip');

            const dependencyHeading = document.createElement('div');
            dependencyHeading.className = 'model-dependency-heading';
            dependencyHeading.textContent = 'Depends on:';
            dependencyPopup.appendChild(dependencyHeading);

            const dependencyList = document.createElement('ul');
            const items = providerTasks.length > 0 ? providerTasks : ['No provider registered'];
            for (const task of items) {
                const dependency = document.createElement('li');
                dependency.textContent = task;
                dependencyList.appendChild(dependency);
            }
            dependencyPopup.appendChild(dependencyList);

            dependencyInfo.append(dependencyIcon, dependencyPopup);
            modelActions.appendChild(dependencyInfo);
        }

        const toggleLabel = document.createElement('label');
        toggleLabel.className = 'model-toggle-switch';
        toggleLabel.setAttribute('aria-label', `Toggle ${presentation.fullLabel}`);

        const toggle = document.createElement('input');
        toggle.type = 'checkbox';
        toggle.setAttribute('role', 'switch');
        toggle.checked = Boolean(model.active);

        const toggleTrack = document.createElement('span');
        toggleTrack.className = 'model-toggle-track';
        toggleTrack.setAttribute('aria-hidden', 'true');

        const toggleThumb = document.createElement('span');
        toggleThumb.className = 'model-toggle-thumb';
        toggleTrack.appendChild(toggleThumb);
        toggleLabel.append(toggle, toggleTrack);
        modelActions.appendChild(toggleLabel);
        item.append(modelInfo, modelActions);

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
