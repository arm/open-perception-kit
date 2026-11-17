/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * Models Management Module
 * Handles fetching and displaying registered AI models.
 */

import { ctrlSend } from "./ctrlws.js"

function readableText(value) {
    return String(value ?? '').trim();
}

function outputs(value) {
    return Array.isArray(value) ? value.filter(item => typeof item?.payloadType === 'string' && item.payloadType.length > 0 && typeof item.producerId === 'string') : [];
}

function inputs(value) {
    return outputs(value).filter(item => typeof item.consumerId === 'string' && item.consumerId.length > 0 &&
        typeof item.inputId === 'string' && item.inputId.length > 0);
}

function dependencyProviderTasks(model, models) {
    const required = inputs(model.requiredInputs);
    return [...new Set(models
        .filter((candidate) => candidate !== model &&
            outputs(candidate.providedOutputs).some(output => required.some(input =>
                output.payloadType === input.payloadType && input.producerId && output.producerId === input.producerId)))
        .map((candidate) => readableText(candidate.task) || readableText(candidate.name))
        .filter(Boolean))];
}

function resolveModelLabels(model) {
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
    return { primaryLabel, secondaryLabel, fullLabel };
}

class ModelsManager {
    constructor() {
        this.container = document.getElementById('models-container');
        this.models = [];
        this.editor = null;
        this.pipelineState = null;
        this.editControls = [];
        this.addButton = document.getElementById('addModelBtn');
        this.editHint = document.getElementById('modelEditHint');
        this.dialog = document.getElementById('modelPicker');
        this.modelChoice = document.getElementById('modelChoice');
        this.modelGroupChoice = document.getElementById('modelGroup');
        this.saveButton = document.getElementById('saveModelBtn');
        this.addButton?.addEventListener('click', () => this.openPicker());
        document.getElementById('cancelModelBtn')?.addEventListener('click', () => this.dialog.close());
        document.getElementById('modelPickerForm')?.addEventListener('submit', (event) => {
            event.preventDefault();
            this.submitEdit({ ...this.pickerRequest, path: this.modelChoice.value, modelGroup: this.modelGroupChoice.value });
            this.dialog.close();
        });
        this._lastModelsSignature = '';
    }

    render(models, editor = null, pipelineState = null) {
        if (!this.container) return;
        this.models = models;
        this.editor = editor;
        this.pipelineState = pipelineState;
        this.editPending = false;
        this.updatePicker();
        this.updateEditorControls();
        const nextSignature = JSON.stringify([models, this.editDisabledReason()]);
        if (nextSignature === this._lastModelsSignature) return;
        this._lastModelsSignature = nextSignature;
        this.container.innerHTML = '';
        this.editControls = [];
        if (models.length === 0) {
            this.container.innerHTML = '<div class="models-empty">No models in this pipeline.</div>';
        }
        let previousModelGroup = null;
        for (const model of models) {
            if (editor?.modelGroups?.length > 1 && model.modelGroup !== previousModelGroup) {
                const heading = document.createElement('p');
                heading.className = 'wfr-txt-caption';
                const modelGroupIndex = editor.modelGroups.findIndex(modelGroup => modelGroup.id === model.modelGroup);
                heading.textContent = modelGroupIndex >= 0 ? `Model group ${modelGroupIndex + 1}` : 'Fixed models';
                this.container.appendChild(heading);
                previousModelGroup = model.modelGroup;
            }
            this.container.appendChild(this.createModelItem(model, models));
        }
        this.updateEditorControls();
    }

    editDisabledReason() {
        const state = this.pipelineState;
        if (!state) return 'Reconnect to the pipeline to edit models.';
        if (state.busy || state.pending || this.editPending) return 'Wait for the current pipeline change to finish.';
        if (state.state !== 'READY') return 'Stop the pipeline to add, replace, remove or reorder models.';
        if (!this.editor?.modelGroups?.length) return 'This pipeline has no editable model group.';
        return '';
    }

    updateEditorControls() {
        const reason = this.editDisabledReason();
        if (this.addButton) {
            this.addButton.disabled = Boolean(reason);
            this.addButton.title = reason || 'Add an installed model';
        }
        if (this.editHint) {
            this.editHint.textContent = reason || 'Drag models to reorder within their group while the pipeline is stopped.';
        }
        for (const { button, limitation } of this.editControls) {
            button.disabled = Boolean(reason || limitation);
            button.title = reason || limitation || button.getAttribute('aria-label');
            if (button.classList.contains('model-drag-handle')) {
                const item = button.closest('.model-item');
                item.draggable = !button.disabled;
                item.title = button.title;
            }
        }
        if (this.saveButton) {
            const staleSelection = this.pickerRequest?.revision !== this.editor?.revision;
            this.saveButton.disabled = Boolean(reason || staleSelection || !this.modelChoice?.value);
            const hint = document.getElementById('modelPickerHint');
            hint.textContent = reason || (staleSelection ? 'The model list changed. Close this dialog and select again.' :
                this.pickerRequest?.action === 'replace' ? 'Replace reloads the selected JSON and clears this model’s input connections, including for the same file.' :
                    'Only OpChains with model files already installed on the server are listed.');
        }
    }

    submitEdit(change) {
        if (this.editDisabledReason()) return;
        try {
            ctrlSend({ type: 'model_edit', revision: this.editor.revision, ...change });
            this.editPending = true;
            this.updateEditorControls();
        } catch (error) {
            this.editHint.textContent = error.message;
        }
    }

    openPicker(model = null) {
        if (this.editDisabledReason()) return;
        this.pickerRequest = {
            action: model ? 'replace' : 'add', name: model?.element_name,
            modelGroup: model?.modelGroup, revision: this.editor.revision
        };
        document.getElementById('modelPickerTitle').textContent = model ? 'Replace model' : 'Add model';
        this.saveButton.textContent = model ? 'Replace' : 'Add';
        this.modelGroupChoice.innerHTML = '';
        for (const [index, modelGroup] of this.editor.modelGroups.entries()) {
            const option = document.createElement('option');
            option.value = modelGroup.id;
            option.textContent = `Model group ${index + 1}`;
            this.modelGroupChoice.appendChild(option);
        }
        this.modelGroupChoice.value = model?.modelGroup || this.editor.modelGroups[0].id;
        this.modelGroupChoice.disabled = Boolean(model);
        document.getElementById('modelGroupLabel').hidden = Boolean(model) || this.editor.modelGroups.length === 1;
        this.pickerPath = model?.opchainPath || '';
        this.availableOpChainsSignature = '';
        this.updatePicker();
        if (model && [...this.modelChoice.options].some(option => option.value === model.opchainPath)) {
            this.modelChoice.value = model.opchainPath;
        }
        this.updateEditorControls();
        this.dialog.showModal();
        try {
            ctrlSend({ type: 'refresh_available_opchains' });
        } catch (error) {
            document.getElementById('modelPickerHint').textContent = error.message;
            this.saveButton.disabled = true;
        }
    }

    updatePicker() {
        if (!this.modelChoice) return;
        const availableOpChains = this.editor?.availableOpChains || [];
        const signature = JSON.stringify(availableOpChains);
        if (signature === this.availableOpChainsSignature) return;
        this.availableOpChainsSignature = signature;
        const selection = this.modelChoice.value || this.pickerPath;
        this.modelChoice.innerHTML = '';
        if (!availableOpChains.length) {
            const empty = document.createElement('option');
            empty.value = '';
            empty.textContent = 'No installed models available';
            this.modelChoice.appendChild(empty);
        }
        for (const opChain of availableOpChains) {
            const option = document.createElement('option');
            option.value = opChain.path;
            option.textContent = `${resolveModelLabels(opChain).fullLabel} — ${opChain.path}`;
            this.modelChoice.appendChild(option);
        }
        if (availableOpChains.some(opChain => opChain.path === selection)) this.modelChoice.value = selection;
    }

    appendEditControls(item, model, models) {
        const toolbar = document.createElement('div');
        toolbar.className = 'model-edit-actions';
        const modelGroupModels = models.filter(candidate => candidate.modelGroup === model.modelGroup);
        const position = modelGroupModels.indexOf(model);
        const modelLabel = resolveModelLabels(model).fullLabel;
        const addButton = (label, text, action, limitation = '') => {
            const button = document.createElement('button');
            button.type = 'button';
            button.className = 'control-button';
            button.textContent = text;
            button.setAttribute('aria-label', `${label} ${modelLabel}`);
            button.setAttribute('aria-describedby', 'modelEditHint');
            button.addEventListener('click', action);
            toolbar.appendChild(button);
            this.editControls.push({ button, limitation: model.modelGroup ? limitation : 'This model is outside an editable model group.' });
            return button;
        };
        const handle = addButton('Drag to reorder', '⠿', () => { },
            modelGroupModels.length < 2 ? 'Add another model to this model group to reorder.' : '');
        handle.classList.add('model-drag-handle');
        handle.setAttribute('aria-keyshortcuts', 'ArrowUp ArrowDown');
        handle.setAttribute('aria-description', 'Use the Up and Down arrow keys to reorder.');
        handle.addEventListener('keydown', event => {
            const direction = event.key === 'ArrowUp' ? -1 : event.key === 'ArrowDown' ? 1 : 0;
            if (!direction || handle.disabled) return;
            event.preventDefault();
            const destination = position + direction;
            const withinModelGroup = destination >= 0 && destination < modelGroupModels.length;
            if (withinModelGroup) {
                this.submitEdit({ action: 'move', name: model.element_name, modelGroup: model.modelGroup, position: destination });
            }
        });
        item.appendChild(handle);
        this.bindModelReordering(item, handle, model, position);
        addButton('Replace', 'Replace', () => this.openPicker(model));
        const remove = addButton('Remove', '', () => this.submitEdit({ action: 'remove', name: model.element_name, modelGroup: model.modelGroup }));
        remove.classList.add('model-remove');
        const icon = document.createElement('span');
        icon.className = 'icon';
        icon.setAttribute('data-icon', 'trash-2');
        icon.setAttribute('aria-hidden', 'true');
        remove.appendChild(icon);
        item.appendChild(toolbar);
        if (model.modelGroup && model.connected === false) {
            const error = document.createElement('p');
            error.className = 'model-link-error wfr-txt-body--sm';
            error.textContent = 'Links incomplete. Edit this model group to reconnect its models.';
            item.appendChild(error);
        }
    }

    bindModelReordering(item, handle, model, position) {
        let dragBlockedByControl = false;
        item.addEventListener('pointerdown', event => {
            const control = event.target.closest('button, input, select, label, a, [tabindex]');
            dragBlockedByControl = Boolean(control && control !== handle);
        });
        item.addEventListener('dragstart', event => {
            if (handle.disabled || dragBlockedByControl) { event.preventDefault(); return; }
            this.draggedModel = { name: model.element_name, modelGroup: model.modelGroup, revision: this.editor.revision };
            item.classList.add('model-dragging');
            const bounds = item.getBoundingClientRect();
            event.dataTransfer.setDragImage(item, event.clientX - bounds.left, event.clientY - bounds.top);
            event.dataTransfer.setData('text/plain', model.element_name);
            event.dataTransfer.effectAllowed = 'move';
        });
        item.addEventListener('dragend', () => {
            item.classList.remove('model-dragging');
            this.draggedModel = null;
        });
        item.addEventListener('dragover', event => {
            if (!this.editDisabledReason() && this.draggedModel?.modelGroup === model.modelGroup) {
                event.preventDefault();
                event.dataTransfer.dropEffect = 'move';
            }
        });
        item.addEventListener('drop', event => {
            event.preventDefault();
            if (this.draggedModel?.modelGroup === model.modelGroup && this.draggedModel.name !== model.element_name) {
                this.submitEdit({ action: 'move', ...this.draggedModel, position });
            }
            this.draggedModel = null;
        });
    }

    createModelItem(model, models) {
        const item = document.createElement('div');
        item.className = 'model-item';
        item.setAttribute('data-model-name', model.name || '');
        item.setAttribute('data-model-element-name', model.element_name || '');
        item.classList.toggle('model-active', Boolean(model.active));

        const modelLabels = resolveModelLabels(model);
        const modelInfo = document.createElement('div');
        modelInfo.className = 'model-info';

        const modelCopy = document.createElement('div');
        modelCopy.className = 'model-copy';
        modelCopy.title = modelLabels.fullLabel;

        const modelTask = document.createElement('div');
        modelTask.className = 'model-task wfr-txt-label';
        modelTask.textContent = modelLabels.primaryLabel;
        modelCopy.appendChild(modelTask);

        if (modelLabels.secondaryLabel) {
            const modelDetails = document.createElement('div');
            modelDetails.className = 'model-details wfr-txt-caption';
            modelDetails.textContent = modelLabels.secondaryLabel;
            modelCopy.appendChild(modelDetails);
        }

        const requiredInputs = inputs(model.requiredInputs);
        const inputSelectors = new Map();
        modelInfo.appendChild(modelCopy);

        for (const input of requiredInputs) {
            const label = document.createElement('label');
            label.className = 'model-input-binding';
            label.textContent = `${input.consumerId}/${input.inputId} — ${input.payloadType.split('.').at(-1)} `;
            const select = document.createElement('select');
            inputSelectors.set(input, select);
            select.setAttribute('aria-label', `Input source for ${modelLabels.fullLabel}, ${input.consumerId}/${input.inputId}, ${input.payloadType}`);
            const addOption = (id, text, disabled = false) => {
                const option = document.createElement('option');
                option.value = id;
                option.textContent = text;
                option.disabled = disabled;
                select.appendChild(option);
            };
            addOption('', 'Not connected');
            const candidates = models.filter(candidate => candidate.element_name !== model.element_name)
                .flatMap(candidate => outputs(candidate.providedOutputs)
                    .filter(output => output.producerId && output.payloadType === input.payloadType &&
                        outputs(model.availableInputs).some(upstream => upstream.payloadType === output.payloadType &&
                            upstream.producerId === output.producerId))
                    .map(output => ({ ...output, label: resolveModelLabels(candidate).fullLabel })));
            const counts = new Map();
            for (const candidate of candidates) counts.set(candidate.producerId, (counts.get(candidate.producerId) || 0) + 1);
            const seen = new Set();
            for (const candidate of candidates) {
                if (seen.has(candidate.producerId)) continue;
                seen.add(candidate.producerId);
                const ambiguous = counts.get(candidate.producerId) !== 1;
                addOption(candidate.producerId, `${candidate.label} — ${candidate.producerId}${ambiguous ? ' (ambiguous)' : ''}`, ambiguous);
            }
            if (input.producerId && !seen.has(input.producerId))
                addOption(input.producerId, `Unavailable: ${input.producerId}`, true);
            select.value = input.producerId;
            select.addEventListener('change', () => {
                const bindings = requiredInputs.map(entry => ({
                    consumerId: entry.consumerId, inputId: entry.inputId,
                    payloadType: entry.payloadType, producerId: inputSelectors.get(entry).value
                }));
                this._lastModelsSignature = '';
                try {
                    ctrlSend({ type: 'model_inputs', name: model.element_name, bindings });
                } catch (error) {
                    select.value = input.producerId;
                    console.error('Error connecting model input:', error);
                }
            });
            label.appendChild(select);
            modelCopy.appendChild(label);
        }

        const modelActions = document.createElement('div');
        modelActions.className = 'model-actions';

        if (requiredInputs.length > 0) {
            const providerTasks = dependencyProviderTasks(model, models);
            const accessibleProviders = providerTasks.length > 0
                ? providerTasks.join(', ')
                : requiredInputs.some(input => input.producerId) ? 'Selected source unavailable' : 'No input connected';
            const dependencyInfo = document.createElement('div');
            dependencyInfo.className = 'model-dependency-info';
            dependencyInfo.setAttribute('tabindex', '0');
            dependencyInfo.setAttribute('aria-label', `Depends on: ${accessibleProviders}`);

            const dependencyIcon = document.createElement('span');
            dependencyIcon.className = 'model-dependency-icon icon';
            dependencyIcon.setAttribute('data-icon', 'info');
            dependencyIcon.setAttribute('aria-hidden', 'true');

            const dependencyPopup = document.createElement('div');
            dependencyPopup.className = 'model-dependency-popup wfr-txt-caption';
            dependencyPopup.setAttribute('role', 'tooltip');

            const dependencyHeading = document.createElement('div');
            dependencyHeading.className = 'model-dependency-heading';
            dependencyHeading.textContent = 'Depends on:';
            dependencyPopup.appendChild(dependencyHeading);

            const dependencyList = document.createElement('ul');
            const items = providerTasks.length > 0 ? providerTasks : [accessibleProviders];
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
        toggleLabel.setAttribute('aria-label', `Toggle ${modelLabels.fullLabel}`);

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
        this.appendEditControls(item, model, models);

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


}

export const modelsManager = new ModelsManager();
