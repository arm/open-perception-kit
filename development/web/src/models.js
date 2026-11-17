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

import { ctrlSend, MODEL_EDIT_SESSION } from "./ctrlws.js"

const MODEL_EDIT_ACK_TIMEOUT_MS = 15000;

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
        this.serverModels = [];
        this.saveInProgress = false;
        this.pendingModelEdit = null;
        this.editor = null;
        this.pipelineState = null;
        this.editControls = [];
        this.editMode = false;
        this.editRequested = false;
        this.draftEdits = [];
        this.nextDraftModelId = 0;
        this.insertionButtons = [];
        this.insertionTargets = new Map();
        this.editPipelineButton = document.getElementById('editPipelineBtn');
        this.discardButton = document.getElementById('discardPipelineBtn');
        this.discardButton?.addEventListener('click', () => this.discardEdits());
        this.editHint = document.getElementById('modelEditHint');
        this.dialog = document.getElementById('modelPicker');
        this.modelChoice = document.getElementById('modelChoice');
        this.modelPickerSaveButton = document.getElementById('saveModelBtn');
        this.editPipelineButton?.addEventListener('click', () => {
            if (this.editDisabledReason()) return;
            if (this.editMode) this.saveEdits();
            else this.beginEditing();
        });
        document.getElementById('cancelModelBtn')?.addEventListener('click', () => this.dialog.close());
        document.getElementById('modelPickerForm')?.addEventListener('submit', (event) => {
            event.preventDefault();
            this.submitEdit({ ...this.pickerRequest, path: this.modelChoice.value });
            this.dialog.close();
        });
        this._lastModelsSignature = '';
    }

    render(models, editor = null, pipelineState = null) {
        if (!this.container) return;
        this.serverModels = models;
        this.editor = editor;
        this.pipelineState = pipelineState;
        if (!pipelineState) this.editRequested = false;
        if (this.saveInProgress) {
            this.acceptSaveReport(models, editor, pipelineState);
        } else if (this.editMode) {
            const draftInvalidated = editor?.configurationId !== this.draftConfigurationId ||
                pipelineState?.state !== 'READY';
            if (draftInvalidated) {
                this.discardEdits();
                this.editNotice = 'The pipeline changed outside this editor. The unsaved draft was discarded.';
            }
        }
        if (!this.editMode) this.models = models;
        this.repaint();
    }

    repaint() {
        this.updatePicker();
        this.updateEditorControls();
        const nextSignature = JSON.stringify([
            this.models, this.editor?.modelGroups, this.editDisabledReason(), this.editMode,
            this.liveControlsDisabledReason()
        ]);
        if (nextSignature === this._lastModelsSignature) return;
        this._lastModelsSignature = nextSignature;
        this.clearDragPreview();
        this.clearDropTarget();
        this.draggedModel = null;
        this.container.innerHTML = '';
        this.editControls = [];
        this.insertionButtons = [];
        this.insertionTargets = new Map();
        if (this.models.length === 0 && !this.editMode) {
            this.container.innerHTML = '<div class="models-empty">No models in this pipeline.</div>';
        }
        this.appendModelGroups();
        this.updateEditorControls();
    }

    appendModelGroups() {
        const remainingGroups = [...(this.editor?.modelGroups || [])];
        const renderedGroups = new Set();
        for (const model of this.models) {
            if (renderedGroups.has(model.modelGroup) && model.modelGroup) continue;
            if (!model.modelGroup) {
                this.container.appendChild(this.createModelItem(model, this.models));
                continue;
            }
            const groupIndex = remainingGroups.findIndex(group => group.id === model.modelGroup);
            for (const group of remainingGroups.splice(0, groupIndex + 1)) {
                this.appendModelGroup(group.id);
                renderedGroups.add(group.id);
            }
            if (!renderedGroups.has(model.modelGroup)) {
                this.appendModelGroup(model.modelGroup);
                renderedGroups.add(model.modelGroup);
            }
        }
        for (const group of remainingGroups) {
            this.appendModelGroup(group.id);
        }
    }

    appendModelGroup(modelGroupId) {
        const groupModels = this.models.filter(model => model.modelGroup === modelGroupId);
        if (!groupModels.length && !this.editMode) return;
        if (this.editor?.modelGroups?.length > 1) {
            const heading = document.createElement('p');
            heading.className = 'wfr-txt-caption';
            const groupIndex = this.editor.modelGroups.findIndex(group => group.id === modelGroupId);
            heading.textContent = `Model group ${groupIndex + 1}`;
            this.container.appendChild(heading);
        }
        if (this.editMode) this.appendInsertionButton(modelGroupId, 0);
        for (const [position, model] of groupModels.entries()) {
            this.container.appendChild(this.createModelItem(model, this.models));
            if (this.editMode) this.appendInsertionButton(modelGroupId, position + 1);
        }
    }

    beginEditing() {
        if (this.editDisabledReason()) return;
        if (this.editor.editing) {
            this.showControlError('This pipeline is being edited in another session.');
            return;
        }
        this.editRequested = true;
        this.editNotice = '';
        try {
            ctrlSend({type: MODEL_EDIT_SESSION, active: true});
        } catch (error) {
            this.editRequested = false;
            this.showControlError(error.message);
        }
        this.updateEditorControls();
    }

    acceptEditSession({active, error}) {
        if (!this.editRequested) return;
        this.editRequested = false;
        if (!active || !this.pipelineState || this.pipelineState.state !== 'READY') {
            if (active) this.releaseEditSession();
            this.showControlError(error || 'Stop the pipeline before editing models.');
            return;
        }
        this.editMode = true;
        this.draftConfigurationId = this.editor.configurationId;
        this.draftEdits = [];
        this.editNotice = '';
        this.models = structuredClone(this.serverModels);
        this.repaint();
    }

    showControlError(message) {
        if (this.saveInProgress) {
            this.failSave(message);
            return;
        }
        this.editNotice = message;
        this.updateEditorControls();
    }

    discardEdits() {
        if (this.saveInProgress) return;
        if (this.editMode) this.releaseEditSession();
        this.editMode = false;
        this.draftEdits = [];
        this.dialog?.close();
        this.models = this.serverModels;
        this.repaint();
    }

    releaseEditSession() {
        if (!this.pipelineState) return;
        try {
            ctrlSend({type: MODEL_EDIT_SESSION, active: false});
        } catch (error) {
            this.editNotice = error.message;
        }
    }

    saveEdits() {
        if (this.editDisabledReason() || !this.editMode) return;
        if (!this.draftEdits.length) { this.discardEdits(); return; }
        this.saveInProgress = true;
        this.remainingModelEdits = [...this.draftEdits];
        this.savedModelNames = new Map();
        this.sendNextEdit();
    }

    sendNextEdit() {
        if (!this.remainingModelEdits.length) {
            this.saveInProgress = false;
            this.discardEdits();
            return;
        }
        const { draftName, configurationId, ...change } = this.remainingModelEdits[0];
        if (change.name) change.name = this.savedModelNames.get(change.name) || change.name;
        this.pendingModelEdit = {
            configurationId: this.editor.configurationId, change, draftName,
            modelNames: this.serverModels.filter(model => model.modelGroup === change.modelGroup)
                .map(model => model.element_name)
        };
        try {
            ctrlSend({ type: 'model_edit', ...change, configurationId: this.editor.configurationId });
            this.saveTimeout = setTimeout(() => this.failSave('The server did not confirm the save. Reconnect and check the model list before retrying.'), MODEL_EDIT_ACK_TIMEOUT_MS);
        } catch (error) {
            this.failSave(error.message);
        }
        this.updateEditorControls();
    }

    acceptSaveReport(models, editor, state) {
        if (!this.pendingModelEdit || state?.busy || state?.pending) return;
        if (!state || state.state !== 'READY' || state.error) {
            this.failSave(state?.error || 'The pipeline is no longer stopped.');
            return;
        }
        if (editor?.configurationId === this.pendingModelEdit.configurationId) return;
        if (editor?.configurationId !== this.pendingModelEdit.configurationId + 1 ||
            !this.savedEditMatchesReport(models)) {
            this.failSave('The pipeline changed in another client.');
            return;
        }
        clearTimeout(this.saveTimeout);
        if (this.pendingModelEdit.draftName) {
            const added = models.find(model => model.modelGroup === this.pendingModelEdit.change.modelGroup &&
                !this.pendingModelEdit.modelNames.includes(model.element_name));
            if (!added) { this.failSave('The added model could not be identified.'); return; }
            this.savedModelNames.set(this.pendingModelEdit.draftName, added.element_name);
        }
        this.pendingModelEdit = null;
        this.remainingModelEdits.shift();
        this.sendNextEdit();
    }

    savedEditMatchesReport(models) {
        const { change, modelNames } = this.pendingModelEdit;
        const groupModels = models.filter(model => model.modelGroup === change.modelGroup);
        const expectedNames = [...modelNames];
        const targetIndex = expectedNames.indexOf(change.name);
        if (change.action === 'add') {
            const addedModels = groupModels.filter(model => !modelNames.includes(model.element_name));
            if (addedModels.length !== 1 || addedModels[0].opchainPath !== change.path) return false;
            expectedNames.splice(change.position ?? expectedNames.length, 0, addedModels[0].element_name);
        } else if (change.action === 'remove') {
            expectedNames.splice(targetIndex, 1);
        } else if (change.action === 'move') {
            expectedNames.splice(targetIndex, 1);
            expectedNames.splice(change.position, 0, change.name);
        } else if (change.action === 'replace') {
            const replaced = groupModels.find(model => model.element_name === change.name);
            if (replaced?.opchainPath !== change.path) return false;
        }
        return JSON.stringify(expectedNames) === JSON.stringify(groupModels.map(model => model.element_name));
    }

    failSave(message) {
        clearTimeout(this.saveTimeout);
        this.pendingModelEdit = null;
        this.saveInProgress = false;
        this.editNotice = `Save stopped: ${message} Showing the server model list; earlier saved changes may have applied.`;
        this.discardEdits();
    }

    appendInsertionButton(modelGroup, position) {
        const row = document.createElement('div');
        row.className = 'model-insertion';
        this.insertionTargets.set(JSON.stringify([modelGroup, position]), row);
        this.bindModelDropTarget(row, modelGroup, () => position);
        row.addEventListener('dragleave', event => {
            if (!row.contains(event.relatedTarget) && this.dropTarget === row) this.clearDropTarget();
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
        if (this.editRequested) return 'Requesting permission to edit the pipeline…';
        if (this.saveInProgress) return 'Saving pipeline edits…';
        const state = this.pipelineState;
        if (!state) return 'Reconnect to the pipeline to edit models.';
        // Draft changes stay local; refreshing available files must not interrupt a picker click.
        const serverCommandBlocksEditing = state.busy && !this.editMode;
        if (serverCommandBlocksEditing || state.pending) return 'Wait for the current pipeline change to finish.';
        if (state.state !== 'READY') return 'Stop the pipeline to add, replace, remove or reorder models.';
        if (!this.editor?.modelGroups?.length) return 'This pipeline has no editable model group.';
        return '';
    }

    updateEditorControls() {
        const reason = this.editDisabledReason();
        if (this.editPipelineButton) {
            this.editPipelineButton.disabled = Boolean(reason);
            const label = this.editMode ? 'Save' : 'Edit Pipeline';
            this.editPipelineButton.title = !this.editMode && this.pipelineState && this.pipelineState.state !== 'READY'
                ? 'Stop pipeline to edit' : reason || label;
            this.editPipelineButton.setAttribute('aria-label', label);
            this.editPipelineButton.querySelector('.edit-pipeline-label').textContent = label;
            this.editPipelineButton.querySelector('.icon').setAttribute('data-icon', this.editMode ? 'check' : 'pencil');
            this.editPipelineButton.setAttribute('aria-pressed', String(this.editMode));
        }
        if (this.discardButton) {
            this.discardButton.hidden = !this.editMode;
            this.discardButton.disabled = Boolean(this.saveInProgress);
        }
        const playButton = document.getElementById('playPauseBtn');
        if (playButton) {
            playButton.disabled = this.editMode || this.editRequested || this.editor?.editing ||
                !this.pipelineState || Boolean(this.pipelineState.busy || this.pipelineState.pending);
            playButton.title = this.editMode
                ? 'Save or discard changes to run the pipeline again'
                : this.editor?.editing ? 'This pipeline is being edited in another session.'
                    : playButton.getAttribute('aria-label');
        }
        this.updateDisabledTooltip('editTooltipAnchor', 'editDisabledTooltip', this.editPipelineButton,
            this.editPipelineButton?.disabled ? this.editPipelineButton.title : '');
        this.updateDisabledTooltip('playTooltipAnchor', 'playDisabledTooltip', playButton,
            this.editMode ? 'Save or discard changes to run the pipeline again' : '');
        for (const button of this.insertionButtons) {
            button.disabled = Boolean(reason);
        }
        if (this.editHint) {
            this.editHint.textContent = reason || this.editNotice || '';
            this.editHint.hidden = !this.editHint.textContent;
        }
        for (const { control, label, limitation, item, toolbar, editableModelGroup } of this.editControls) {
            const editable = this.editMode && editableModelGroup;
            control.hidden = !editable;
            toolbar.hidden = !editable;
            item.classList.toggle('model-editable', editable);
            const disabled = Boolean(!editable || reason || limitation);
            control.title = reason || limitation || label;
            if (control.classList.contains('model-drag-handle')) {
                control.setAttribute('aria-disabled', String(disabled));
                item.draggable = !disabled;
            } else {
                control.disabled = disabled;
            }
        }
        if (this.modelPickerSaveButton) {
            const staleSelection = this.pickerRequest?.configurationId !== this.editor?.configurationId;
            this.modelPickerSaveButton.disabled = Boolean(reason || staleSelection || !this.modelChoice?.value);
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
        if (!this.editMode || this.editDisabledReason() ||
            !this.editor.modelGroups.some(group => group.id === change.modelGroup)) {
            return;
        }
        const modelGroupModels = this.models.filter(model => model.modelGroup === change.modelGroup);
        const target = modelGroupModels.find(model => model.element_name === change.name);
        const hasValidTarget = change.action === 'add' || target !== undefined;
        const hasValidPosition = change.position === undefined ||
            (Number.isInteger(change.position) && change.position >= 0 &&
                change.position < modelGroupModels.length + (change.action === 'add' ? 1 : 0));
        if (!hasValidTarget || !hasValidPosition) return;
        const opChainEntry = this.editor.availableOpChains?.find(opChain => opChain.path === change.path);
        if (change.action === 'add' || change.action === 'replace') {
            if (!opChainEntry) return;
            const replacement = { ...opChainEntry, opchainPath: change.path, modelGroup: change.modelGroup,
                element_name: target?.element_name || this.createDraftModelName(),
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
        } else {
            return;
        }
        const groupIndex = this.editor.modelGroups.findIndex(group => group.id === change.modelGroup);
        const followingGroups = this.editor.modelGroups.slice(groupIndex).map(group => group.id);
        const first = this.models.findIndex(model => followingGroups.includes(model.modelGroup));
        this.models = this.models.filter(model => model.modelGroup !== change.modelGroup);
        this.models.splice(first < 0 ? this.models.length : first, 0, ...modelGroupModels);
        this.draftEdits.push(change);
        this.repaint();
    }

    createDraftModelName() {
        let name;
        do {
            name = `draft-model-${++this.nextDraftModelId}`;
        } while (this.models.some(model => model.element_name === name));
        return name;
    }

    openPicker(model = null, insertion = null) {
        if (!this.editMode || this.editDisabledReason()) return;
        this.pickerRequest = {
            action: model ? 'replace' : 'add', name: model?.element_name,
            modelGroup: model?.modelGroup, configurationId: this.editor.configurationId, ...insertion
        };
        document.getElementById('modelPickerTitle').textContent = model ? 'Replace model' : 'Add model';
        this.modelPickerSaveButton.textContent = model ? 'Replace' : 'Add';
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
            this.modelPickerSaveButton.disabled = true;
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
            this.editControls.push({ control: button, label: `${label} ${modelLabel}`, item, toolbar, editableModelGroup: Boolean(model.modelGroup),
                limitation: model.modelGroup ? limitation : 'This model is outside an editable model group.' });
            return button;
        };
        const handle = document.createElement('span');
        handle.className = 'model-drag-handle';
        handle.textContent = '⠿';
        handle.setAttribute('aria-hidden', 'true');
        this.editControls.push({ control: handle, label: `Drag to reorder ${modelLabel}`, item, toolbar,
            editableModelGroup: Boolean(model.modelGroup),
            limitation: modelGroupModels.length < 2 ? 'Add another model to this model group to reorder.' : '' });
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

    bindModelDropTarget(element, modelGroup, boundaryAt) {
        element.addEventListener('dragover', event => {
            this.clearDropTarget();
            const boundary = boundaryAt(event);
            if (this.dropDestination(modelGroup, boundary) === null) return;
            event.preventDefault();
            event.dataTransfer.dropEffect = 'move';
            this.showDropTarget(modelGroup, boundary);
        });
        element.addEventListener('drop', event => {
            event.preventDefault();
            const destination = this.dropDestination(modelGroup, boundaryAt(event));
            this.clearDragPreview();
            this.clearDropTarget();
            if (destination !== null) this.submitEdit({ action: 'move', ...this.draggedModel, position: destination });
            this.draggedModel = null;
        });
    }

    bindModelReordering(item, handle, model, position) {
        this.bindModelDropTarget(item, model.modelGroup, event => {
            const bounds = item.getBoundingClientRect();
            const midpoint = bounds.top + bounds.height / 2;
            return position + (event.clientY >= midpoint ? 1 : 0);
        });
        item.addEventListener('pointerdown', event => {
            const control = event.target.closest('button, input, select, textarea, a');
            item.draggable = handle.getAttribute('aria-disabled') === 'false' && !control;
        });
        item.addEventListener('dragstart', event => {
            if (!item.draggable || handle.getAttribute('aria-disabled') !== 'false') { event.preventDefault(); return; }
            this.clearDropTarget();
            this.draggedModel = { name: model.element_name, modelGroup: model.modelGroup, configurationId: this.editor.configurationId, position };
            item.classList.add('model-dragging');
            const bounds = item.getBoundingClientRect();
            const preview = this.createDragPreview(item, bounds);
            event.dataTransfer.setDragImage(preview, event.clientX - bounds.left, event.clientY - bounds.top);
            event.dataTransfer.setData('text/plain', model.element_name);
            event.dataTransfer.effectAllowed = 'move';
        });
        item.addEventListener('dragend', () => {
            item.classList.remove('model-dragging');
            this.clearDragPreview();
            this.clearDropTarget();
            this.draggedModel = null;
        });
        item.addEventListener('dragleave', event => {
            if (!item.contains(event.relatedTarget)) this.clearDropTarget();
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

        const modelInfo = this.createModelInfo(model);
        this.appendInputSelectors(modelInfo.querySelector('.model-copy'), item, model, models);
        const modelActions = document.createElement('div');
        modelActions.className = 'model-actions';
        if (inputs(model.requiredInputs).length) {
            modelActions.appendChild(this.createDependencyInfo(model, models));
        }
        modelActions.appendChild(this.createModelToggle(model, item));
        item.append(modelInfo, modelActions);
        this.appendEditControls(item, model, models);
        return item;
    }

    createModelInfo(model) {
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

        modelInfo.appendChild(modelCopy);
        return modelInfo;
    }

    appendInputSelectors(modelCopy, item, model, models) {
        const modelLabels = resolveModelLabels(model);
        const requiredInputs = inputs(model.requiredInputs);
        const inputSelectors = new Map();
        for (const input of requiredInputs) {
            const label = document.createElement('label');
            label.className = 'model-input-binding';
            label.textContent = `${input.consumerId}/${input.inputId} — ${input.payloadType.split('.').at(-1)} `;
            const select = document.createElement('select');
            inputSelectors.set(input, select);
            select.disabled = Boolean(this.liveControlsDisabledReason());
            select.title = this.liveControlsDisabledReason();
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
            for (const candidate of candidates) {
                counts.set(candidate.producerId, (counts.get(candidate.producerId) || 0) + 1);
            }
            const seen = new Set();
            for (const candidate of candidates) {
                if (seen.has(candidate.producerId)) continue;
                seen.add(candidate.producerId);
                const ambiguous = counts.get(candidate.producerId) !== 1;
                addOption(candidate.producerId, `${candidate.label} — ${candidate.producerId}${ambiguous ? ' (ambiguous)' : ''}`, ambiguous);
            }
            if (input.producerId && !seen.has(input.producerId)) {
                addOption(input.producerId, `Unavailable: ${input.producerId}`, true);
            }
            select.value = input.producerId;
            select.addEventListener('change', () => {
                if (this.liveControlsDisabledReason()) return;
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
    }

    createDependencyInfo(model, models) {
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
        return dependencyInfo;
    }

    createModelToggle(model, item) {
        const toggleLabel = document.createElement('label');
        toggleLabel.className = 'model-toggle-switch';

        const toggle = document.createElement('input');
        toggle.type = 'checkbox';
        toggle.setAttribute('role', 'switch');
        toggle.checked = Boolean(model.active);
        toggle.setAttribute('aria-label', `Toggle ${resolveModelLabels(model).fullLabel}`);
        toggle.disabled = Boolean(this.liveControlsDisabledReason());
        toggle.title = this.liveControlsDisabledReason();

        const toggleTrack = document.createElement('span');
        toggleTrack.className = 'model-toggle-track';
        toggleTrack.setAttribute('aria-hidden', 'true');

        const toggleThumb = document.createElement('span');
        toggleThumb.className = 'model-toggle-thumb';
        toggleTrack.appendChild(toggleThumb);
        toggleLabel.append(toggle, toggleTrack);

        toggle.addEventListener('change', () => {
            const shouldBeActive = toggle.checked;
            this.handleToggle(model, shouldBeActive, item, toggle);
        });

        return toggleLabel;
    }

    liveControlsDisabledReason() {
        if (this.editMode) return 'Save or discard model list edits before changing models or input connections.';
        if (this.editRequested || this.editor?.editing) return 'This pipeline is being edited in another session.';
        if (!this.pipelineState) return 'Reconnect to the pipeline to change models or input connections.';
        if (this.pipelineState.busy || this.pipelineState.pending) return 'Wait for the current pipeline change to finish.';
        return '';
    }

    handleToggle(model, shouldBeActive, item, toggle) {
        if (this.liveControlsDisabledReason() || model.active === shouldBeActive) {
            return;
        }

        const previousActive = model.active;
        const hadFocus = document.activeElement === toggle;

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
            if (hadFocus) toggle.focus();
        }
    }
}

export const modelsManager = new ModelsManager();
