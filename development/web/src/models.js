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
    return [...new Set(inputs(model.requiredInputs).flatMap(input => {
        if (!input.producerId) return ['No input connected'];
        const providers = models.filter(candidate => candidate.element_name !== model.element_name &&
            outputs(candidate.providedOutputs).some(output =>
                output.payloadType === input.payloadType && output.producerId === input.producerId));
        if (!providers.length) return ['Selected source unavailable'];
        return providers.map(candidate => {
            const label = readableText(candidate.task) || readableText(candidate.name) || 'Unknown model';
            return candidate.active === false ? `${label} (off)` : label;
        });
    }))];
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
        this.editMode = false;
        this.draftEdits = [];
        this.draftCounter = 0;
        this.insertionButtons = [];
        this.insertionTargets = new Map();
        this.addButton = document.getElementById('editPipelineBtn');
        this.discardButton = document.getElementById('discardPipelineBtn');
        this.discardButton?.addEventListener('click', () => this.discardEdits());
        this.editHint = document.getElementById('modelEditHint');
        this.dialog = document.getElementById('modelPicker');
        this.modelChoice = document.getElementById('modelChoice');
        this.modelGroupChoice = document.getElementById('modelGroup');
        this.saveButton = document.getElementById('saveModelBtn');
        this.addButton?.addEventListener('click', () => {
            if (this.editDisabledReason()) return;
            if (this.editMode) this.saveEdits();
            else this.beginEditing();
        });
        document.getElementById('cancelModelBtn')?.addEventListener('click', () => this.dialog.close());
        document.getElementById('modelPickerForm')?.addEventListener('submit', (event) => {
            event.preventDefault();
            this.submitEdit({ ...this.pickerRequest, path: this.modelChoice.value, modelGroup: this.modelGroupChoice.value });
            this.dialog.close();
        });
        this._lastModelsSignature = '';
    }

    render(models, editor = null, pipelineState = null, local = false) {
        if (!this.container) return;
        if (!local) {
            this.serverModels = models;
            this.editor = editor;
            this.pipelineState = pipelineState;
            if (this.saving) {
                this.acceptSaveReport(models, editor, pipelineState);
            } else if (this.editMode && this.draftRevision !== undefined &&
                (editor?.revision !== this.draftRevision || pipelineState?.state !== 'READY')) {
                this.editMode = false;
                this.draftEdits = [];
                this.editNotice = 'The pipeline changed outside this editor. The unsaved draft was discarded.';
            }
            if (this.editMode) models = this.models;
        }
        this.models = models;
        this.editor = editor;
        this.pipelineState = pipelineState;
        this.editPending = false;
        if (pipelineState?.state !== 'READY' && !this.saving) this.editMode = false;
        this.updatePicker();
        this.updateEditorControls();
        const nextSignature = JSON.stringify([models, editor?.modelGroups, this.editDisabledReason(), this.editMode]);
        if (nextSignature === this._lastModelsSignature) return;
        this._lastModelsSignature = nextSignature;
        this.container.innerHTML = '';
        this.editControls = [];
        this.insertionButtons = [];
        this.insertionTargets = new Map();
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
            const modelGroupModels = models.filter(candidate => candidate.modelGroup === model.modelGroup);
            const position = modelGroupModels.indexOf(model);
            if (this.editMode && model.modelGroup && position === 0) {
                this.appendInsertionButton(model.modelGroup, 0);
            }
            this.container.appendChild(this.createModelItem(model, models));
            if (this.editMode && model.modelGroup) this.appendInsertionButton(model.modelGroup, position + 1);
        }
        if (this.editMode) {
            for (const modelGroup of editor.modelGroups) {
                if (!models.some(model => model.modelGroup === modelGroup.id)) this.appendInsertionButton(modelGroup.id, 0);
            }
        }
        this.updateEditorControls();
    }

    beginEditing() {
        if (this.editDisabledReason()) return;
        this.editMode = true;
        this.draftRevision = this.editor.revision;
        this.draftEdits = [];
        this.editNotice = '';
        this.models = structuredClone(this.serverModels || this.models);
        this.repaint();
    }

    repaint() {
        this.render(this.models, this.editor, this.pipelineState, true);
    }

    discardEdits() {
        if (this.saving) return;
        this.editMode = false;
        this.draftEdits = [];
        this.models = this.serverModels || this.models;
        this.repaint();
    }

    saveEdits() {
        if (this.editDisabledReason() || !this.editMode) return;
        if (!this.draftEdits.length) { this.discardEdits(); return; }
        this.saving = true;
        this.saveQueue = [...this.draftEdits];
        this.savedNames = new Map();
        this.sendNextEdit();
    }

    sendNextEdit() {
        if (!this.saveQueue.length) {
            this.saving = false;
            this.discardEdits();
            return;
        }
        const { draftName, revision, ...change } = this.saveQueue[0];
        if (change.name) change.name = this.savedNames.get(change.name) || change.name;
        this.pendingSave = { revision: this.editor.revision, names: new Set(this.serverModels.map(model => model.element_name)), draftName };
        try {
            ctrlSend({ type: 'model_edit', ...change, revision: this.editor.revision });
            this.saveTimer = setTimeout(() => this.failSave('The server did not confirm the save. Reconnect and check the model list before retrying.'), 15000);
        } catch (error) {
            this.failSave(error.message);
        }
        this.updateEditorControls();
    }

    acceptSaveReport(models, editor, state) {
        if (!this.pendingSave || state?.busy || state?.pending) return;
        if (!state || state.state !== 'READY' || state.error) {
            this.failSave(state?.error || 'The pipeline is no longer stopped.');
            return;
        }
        if (editor.revision === this.pendingSave.revision) return;
        if (editor.revision !== this.pendingSave.revision + 1) {
            this.failSave('The pipeline changed in another client.');
            return;
        }
        clearTimeout(this.saveTimer);
        if (this.pendingSave.draftName) {
            const added = models.find(model => !this.pendingSave.names.has(model.element_name));
            if (!added) { this.failSave('The added model could not be identified.'); return; }
            this.savedNames.set(this.pendingSave.draftName, added.element_name);
        }
        this.pendingSave = null;
        this.saveQueue.shift();
        this.sendNextEdit();
    }

    failSave(message) {
        clearTimeout(this.saveTimer);
        this.pendingSave = null;
        this.saving = false;
        this.editNotice = `Save stopped: ${message} Showing the server model list; earlier saved changes may have applied.`;
        this.discardEdits();
    }

    appendInsertionButton(modelGroup, position) {
        const row = document.createElement('div');
        row.className = 'model-insertion';
        this.insertionTargets.set(JSON.stringify([modelGroup, position]), row);
        row.addEventListener('dragover', event => {
            this.clearDropTarget();
            if (this.dropDestination(modelGroup, position) === null) return;
            event.preventDefault();
            event.dataTransfer.dropEffect = 'move';
            this.showDropTarget(modelGroup, position);
        });
        row.addEventListener('dragleave', event => {
            if (!row.contains(event.relatedTarget) && this.dropTarget === row) this.clearDropTarget();
        });
        row.addEventListener('drop', event => {
            event.preventDefault();
            const destination = this.dropDestination(modelGroup, position);
            this.clearDragPreview();
            this.clearDropTarget();
            if (destination !== null) this.submitEdit({ action: 'move', ...this.draggedModel, position: destination });
            this.draggedModel = null;
        });
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'control-button model-insert-button';
        const icon = document.createElement('span');
        icon.className = 'icon';
        icon.setAttribute('data-icon', 'plus');
        icon.setAttribute('aria-hidden', 'true');
        button.appendChild(icon);
        button.title = 'Add model';
        button.setAttribute('aria-label', `Add model at position ${position + 1}`);
        button.addEventListener('click', () => this.openPicker(null, { modelGroup, position }));
        row.appendChild(button);
        this.container.appendChild(row);
        this.insertionButtons.push(button);
    }

    editDisabledReason() {
        if (this.saving) return 'Saving pipeline edits…';
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
            const label = this.editMode ? 'Save' : 'Edit Pipeline';
            this.addButton.title = !this.editMode && this.pipelineState && this.pipelineState.state !== 'READY'
                ? 'Stop pipeline to edit' : reason || label;
            this.addButton.setAttribute('aria-label', label);
            this.addButton.querySelector('.edit-pipeline-label').textContent = label;
            this.addButton.querySelector('.icon').setAttribute('data-icon', this.editMode ? 'check' : 'pencil');
            this.addButton.setAttribute('aria-pressed', String(this.editMode));
        }
        if (this.discardButton) {
            this.discardButton.hidden = !this.editMode;
            this.discardButton.disabled = Boolean(this.saving);
        }
        const playButton = document.getElementById('playPauseBtn');
        if (playButton) {
            playButton.disabled = this.editMode || !this.pipelineState || Boolean(this.pipelineState.busy || this.pipelineState.pending);
            playButton.title = this.editMode
                ? 'Save or discard changes to run the pipeline again'
                : playButton.getAttribute('aria-label');
        }
        this.updateDisabledTooltip('editTooltipAnchor', 'editDisabledTooltip', this.addButton,
            this.addButton?.disabled ? this.addButton.title : '');
        this.updateDisabledTooltip('playTooltipAnchor', 'playDisabledTooltip', playButton,
            this.editMode ? 'Save or discard changes to run the pipeline again' : '');
        for (const button of this.insertionButtons) button.disabled = Boolean(reason);
        if (this.editHint) {
            this.editHint.textContent = reason || this.editNotice || '';
            this.editHint.hidden = !this.editHint.textContent;
        }
        for (const { button, limitation, item, toolbar, editableModelGroup } of this.editControls) {
            const editable = this.editMode && editableModelGroup;
            button.hidden = !editable;
            toolbar.hidden = !editable;
            item.classList.toggle('model-editable', editable);
            button.disabled = Boolean(!this.editMode || reason || limitation);
            button.title = reason || limitation || button.getAttribute('aria-label');
            if (button.classList.contains('model-drag-handle')) {
                button.draggable = !button.disabled;
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

    updateDisabledTooltip(anchorId, tooltipId, button, message) {
        const anchor = document.getElementById(anchorId);
        const tooltip = document.getElementById(tooltipId);
        if (!anchor || !tooltip || !button) return;
        tooltip.textContent = message;
        anchor.classList.toggle('has-tooltip', Boolean(message));
        anchor.tabIndex = message ? 0 : -1;
        anchor.setAttribute('aria-label', button.getAttribute('aria-label'));
        anchor.setAttribute('aria-describedby', tooltipId);
        if (message) button.title = '';
    }

    submitEdit(change) {
        if (!this.editMode || this.editDisabledReason()) return;
        const modelGroupModels = this.models.filter(model => model.modelGroup === change.modelGroup);
        const target = modelGroupModels.find(model => model.element_name === change.name);
        const opChainEntry = this.editor.availableOpChains?.find(model => model.path === change.path);
        if (change.action === 'add' || change.action === 'replace') {
            if (!opChainEntry) return;
            const replacement = { ...opChainEntry, opchainPath: change.path, modelGroup: change.modelGroup,
                element_name: target?.element_name || `draft-model-${++this.draftCounter}`,
                active: target?.active ?? true, connected: true, requiredInputs: [], providedOutputs: [] };
            if (change.action === 'replace') modelGroupModels.splice(modelGroupModels.indexOf(target), 1, replacement);
            else {
                change = { ...change, draftName: replacement.element_name };
                modelGroupModels.splice(change.position ?? modelGroupModels.length, 0, replacement);
            }
        } else if (change.action === 'remove') {
            modelGroupModels.splice(modelGroupModels.indexOf(target), 1);
        } else if (change.action === 'move') {
            modelGroupModels.splice(modelGroupModels.indexOf(target), 1);
            modelGroupModels.splice(change.position, 0, target);
        }
        const first = this.models.findIndex(model => model.modelGroup === change.modelGroup);
        this.models = this.models.filter(model => model.modelGroup !== change.modelGroup);
        this.models.splice(first < 0 ? this.models.length : first, 0, ...modelGroupModels);
        this.draftEdits.push(change);
        this.repaint();
    }

    openPicker(model = null, insertion = null) {
        if (!this.editMode || this.editDisabledReason()) return;
        this.pickerRequest = {
            action: model ? 'replace' : 'add', name: model?.element_name,
            modelGroup: model?.modelGroup, revision: this.editor.revision, ...insertion
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
        this.modelGroupChoice.value = model?.modelGroup || insertion?.modelGroup || this.editor.modelGroups[0].id;
        this.modelGroupChoice.disabled = Boolean(model || insertion);
        document.getElementById('modelGroupLabel').hidden = Boolean(model || insertion) || this.editor.modelGroups.length === 1;
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
        for (const model of availableOpChains) {
            const option = document.createElement('option');
            option.value = model.path;
            option.textContent = `${resolveModelLabels(model).fullLabel} — ${model.path}`;
            this.modelChoice.appendChild(option);
        }
        if (availableOpChains.some(model => model.path === selection)) this.modelChoice.value = selection;
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
            this.editControls.push({ button, item, toolbar, editableModelGroup: Boolean(model.modelGroup),
                limitation: model.modelGroup ? limitation : 'This model is outside an editable model group.' });
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

    clearDropTarget() {
        this.dropTarget?.classList.remove('model-drop-target');
        this.dropTarget = null;
    }

    dropDestination(modelGroup, boundary) {
        if (!this.editMode || this.editDisabledReason() || this.draggedModel?.modelGroup !== modelGroup) return null;
        const destination = boundary - (this.draggedModel.position < boundary ? 1 : 0);
        return destination === this.draggedModel.position ? null : destination;
    }

    showDropTarget(modelGroup, boundary) {
        this.dropTarget = this.insertionTargets.get(JSON.stringify([modelGroup, boundary])) || null;
        this.dropTarget?.classList.add('model-drop-target');
    }

    clearDragPreview() {
        this.dragPreview?.remove();
        this.dragPreview = null;
    }

    createDragPreview(item, bounds) {
        this.clearDragPreview();
        const preview = item.cloneNode(true);
        preview.classList.add('model-drag-preview');
        preview.setAttribute('aria-hidden', 'true');
        preview.style.width = `${bounds.width}px`;
        preview.style.height = `${bounds.height}px`;
        const originalSelects = item.querySelectorAll('select');
        preview.querySelectorAll('select').forEach((select, index) => {
            select.value = originalSelects[index].value;
        });
        document.body.appendChild(preview);
        this.dragPreview = preview;
        return preview;
    }

    bindModelReordering(item, handle, model, position) {
        const dropPosition = event => {
            const bounds = item.getBoundingClientRect();
            const boundary = position + (event.clientY >= bounds.top + bounds.height / 2 ? 1 : 0);
            const destination = this.dropDestination(model.modelGroup, boundary);
            return destination === null ? null : {boundary, destination};
        };
        handle.addEventListener('dragstart', event => {
            if (handle.disabled) { event.preventDefault(); return; }
            this.clearDropTarget();
            this.draggedModel = { name: model.element_name, modelGroup: model.modelGroup, revision: this.editor.revision, position };
            item.classList.add('model-dragging');
            const bounds = item.getBoundingClientRect();
            const preview = this.createDragPreview(item, bounds);
            event.dataTransfer.setDragImage(preview, event.clientX - bounds.left, event.clientY - bounds.top);
            event.dataTransfer.setData('text/plain', model.element_name);
            event.dataTransfer.effectAllowed = 'move';
        });
        handle.addEventListener('dragend', () => {
            item.classList.remove('model-dragging');
            this.clearDragPreview();
            this.clearDropTarget();
            this.draggedModel = null;
        });
        item.addEventListener('dragover', event => {
            this.clearDropTarget();
            const target = dropPosition(event);
            if (!target) return;
            event.preventDefault();
            event.dataTransfer.dropEffect = 'move';
            this.showDropTarget(model.modelGroup, target.boundary);
        });
        item.addEventListener('dragleave', event => {
            if (!item.contains(event.relatedTarget)) this.clearDropTarget();
        });
        item.addEventListener('drop', event => {
            event.preventDefault();
            const target = dropPosition(event);
            this.clearDragPreview();
            this.clearDropTarget();
            if (target) {
                this.submitEdit({ action: 'move', ...this.draggedModel, position: target.destination });
            }
            this.draggedModel = null;
        });
    }

    updateDependencyInfo(info, model, models) {
        if (!info) return;
        const requiredInputs = inputs(model.requiredInputs);
        const descriptions = dependencyProviderTasks(model, models);
        const items = descriptions.length ? descriptions : ['No input connected'];
        info.setAttribute('aria-label', `Depends on: ${items.join(', ')}`);
        const inactiveSource = model.active === true && models.some(candidate =>
            candidate.element_name !== model.element_name && candidate.active === false &&
            outputs(candidate.providedOutputs).some(output => requiredInputs.some(input =>
                input.producerId && input.producerId === output.producerId &&
                input.payloadType === output.payloadType)));
        info.classList.toggle('model-dependency-warning', inactiveSource);
        const list = info.querySelector('ul');
        list.innerHTML = '';
        for (const description of items) {
            const entry = document.createElement('li');
            entry.textContent = description;
            list.appendChild(entry);
        }
    }

    createModelItem(model, models) {
        const item = document.createElement('div');
        item.className = 'model-item';
        item.classList.toggle('model-edit-mode', this.editMode);
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
            select.disabled = this.editMode;
            if (this.editMode) select.title = 'Save or discard model list edits before changing input connections.';
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
                    this.updateDependencyInfo(item.querySelector('.model-dependency-info'),
                        { ...model, requiredInputs: bindings }, models);
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
            const dependencyInfo = document.createElement('div');
            dependencyInfo.className = 'model-dependency-info';
            dependencyInfo.setAttribute('tabindex', '0');

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
            dependencyPopup.appendChild(dependencyList);

            dependencyInfo.append(dependencyIcon, dependencyPopup);
            this.updateDependencyInfo(dependencyInfo, model, models);
            modelActions.appendChild(dependencyInfo);
        }

        const toggleLabel = document.createElement('label');
        toggleLabel.className = 'model-toggle-switch';
        toggleLabel.setAttribute('aria-label', `Toggle ${modelLabels.fullLabel}`);

        const toggle = document.createElement('input');
        toggle.type = 'checkbox';
        toggle.setAttribute('role', 'switch');
        toggle.checked = Boolean(model.active);
        toggle.disabled = this.editMode;
        if (this.editMode) toggle.title = 'Save or discard model list edits before switching models.';

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
