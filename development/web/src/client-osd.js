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

import {renderOsd} from './osd-renderer.js';
import {OVERLAY_FEATURES, featureFor, overlayStatus, resolveOverlay} from './overlay-features.js';
import {bindPendingOverlays, createOverlay, loadOverlays, saveOverlays} from './overlays.js';
import {indexFrame, sourceDetails, sourceKey, sourceLabels} from './overlay-sources.js';

const canvas = document.getElementById('clientOsdCanvas');
const video = document.getElementById('video');
const toggleButton = document.getElementById('clientOsdControlsBtn');
const panel = document.getElementById('clientOsdControlsPanel');
const list = document.getElementById('overlayList');
const notice = document.getElementById('overlayNotice');
const connection = document.getElementById('overlayConnection');
const addButton = document.getElementById('addOverlayBtn');
const dialog = document.getElementById('overlayDialog');
const form = document.getElementById('overlayForm');
const featureSelect = document.getElementById('overlayFeature');
const outputSelect = document.getElementById('overlayOutput');
const sourceField = document.getElementById('overlaySourceField');
const sourceSummary = document.getElementById('overlaySourceSummary');
const editorDetails = document.getElementById('overlaySourceDetails');
const editorSettings = document.getElementById('overlayEditorSettings');
const editorHint = document.getElementById('overlayEditorHint');
const submitButton = document.getElementById('saveOverlayBtn');

let storage;
try {
    storage = window.localStorage;
} catch {
    // Storage access can be disabled; overlays still work for this page session.
}
const restored = loadOverlays(storage);
const overlays = restored.overlays;
notice.textContent = restored.notice;
let frameIndex = indexFrame(null);
// Drawing uses only metadata matched to a displayed video frame. The live
// metadata channel can still discover sources before video is connected.
let displayedFrameIndex = indexFrame(null);
// Store only descriptors, not old frames or bitmap buffers, for the source picker.
const knownOutputs = new Map();
const cardNodes = new Map();
const expandedOverlays = new Set();
let editing = null;
let draft = null;
let returnFocus = addButton;

function node(tag, className, text) {
    const result = document.createElement(tag);
    if (className) result.className = className;
    if (text !== undefined) result.textContent = text;
    return result;
}

function persist() {
    notice.textContent = saveOverlays(storage, overlays);
}

function setPanelOpen(open) {
    panel.hidden = !open;
    toggleButton.setAttribute('aria-expanded', String(open));
    toggleButton.setAttribute('aria-pressed', String(open));
    if (open) addButton.focus();
}

toggleButton.addEventListener('click', () => setPanelOpen(panel.hidden));

function styleControls(featureId, settings, changed) {
    const container = node('div', 'overlay-style-controls');
    for (const control of featureFor(featureId)?.controls || []) {
        if (control.type === 'checkbox') {
            const label = node('label', 'overlay-visibility');
            const input = document.createElement('input');
            input.type = 'checkbox';
            input.checked = settings[control.key];
            input.addEventListener('change', () => {
                settings[control.key] = input.checked;
                changed();
            });
            label.append(input, node('span', '', control.label));
            container.append(label);
            continue;
        }
        const label = node('label', 'overlay-field');
        label.append(node('span', '', control.label));
        const row = node('span', 'overlay-style-value');
        const input = document.createElement('input');
        input.type = control.type;
        input.setAttribute('aria-label', control.label);
        for (const attribute of ['min', 'max', 'step']) {
            if (control[attribute] !== undefined) input[attribute] = control[attribute];
        }
        input.value = settings[control.key];
        const value = node('output');
        const updateValue = () => {
            value.textContent = control.type === 'range' ? `${Math.round(Number(input.value) * 100)}%` : input.value;
        };
        input.addEventListener('input', () => {
            settings[control.key] = control.type === 'range' ? Number(input.value) : input.value;
            updateValue();
            changed();
        });
        updateValue();
        row.append(input, value);
        label.append(row);
        container.append(label);
    }
    return container;
}

function technicalDetails(source) {
    const description = node('dl', 'overlay-metadata');
    for (const [label, value] of sourceDetails(source)) {
        description.append(node('dt', '', label), node('dd', '', value));
    }
    return description;
}

function selectionKey(overlay) {
    return overlay?.selection.primary ? sourceKey(overlay.selection.primary) : '';
}

function updateSourceLabels() {
    const catalog = new Map(knownOutputs);
    for (const overlay of overlays) {
        if (overlay.selection.primary) catalog.set(selectionKey(overlay), overlay.selection.primary);
    }
    for (const [overlay, row] of cardNodes) {
        const sources = featureFor(overlay.featureId)?.discover(catalog) || [];
        const labels = sourceLabels(overlay.selection.primary ? [...sources, overlay.selection.primary] : sources, false);
        const source = overlay.selection.primary ? labels.get(selectionKey(overlay)) : 'No source selected';
        const title = `${row.featureLabel} · ${source}`;
        row.title.textContent = title;
        row.toggle.title = `${title}\n${row.status.textContent}`;
        row.checkbox.setAttribute('aria-label', `Show ${title}`);
        row.remove.setAttribute('aria-label', `Remove ${title} overlay`);
        if (row.metadata.hidden && overlay.selection.primary) {
            row.metadata.replaceChildren(node('summary', '', 'Details'), technicalDetails(overlay.selection.primary));
            row.metadata.hidden = false;
        }
    }
}

function renderList() {
    cardNodes.clear();
    list.replaceChildren();
    if (!overlays.length) {
        list.append(node('p', 'overlay-hint', 'No overlays. Add an overlay to draw pipeline outputs on the video.'));
    }
    for (const [index, overlay] of overlays.entries()) {
        const card = node('article', 'overlay-card');
        const header = node('div', 'overlay-card-header');
        const featureLabel = featureFor(overlay.featureId)?.label || overlay.featureId;
        const visibility = node('label', 'overlay-visible');
        const checkbox = document.createElement('input');
        checkbox.type = 'checkbox';
        checkbox.checked = overlay.enabled;
        checkbox.addEventListener('change', () => {
            overlay.enabled = checkbox.checked;
            persist();
            updateStatuses();
        });
        visibility.append(checkbox);
        const toggle = node('button', 'overlay-toggle');
        toggle.type = 'button';
        toggle.id = `overlay-toggle-${index}`;
        const chevron = node('i', 'fa-solid fa-chevron-right');
        chevron.setAttribute('aria-hidden', 'true');
        const title = node('span', 'overlay-title');
        toggle.append(chevron, title);
        const status = node('p', 'overlay-hint');
        status.id = `overlay-status-${index}`;
        const settings = node('div', 'overlay-settings');
        settings.id = `overlay-settings-${index}`;
        settings.hidden = !expandedOverlays.has(overlay);
        settings.setAttribute('role', 'group');
        settings.setAttribute('aria-labelledby', toggle.id);
        toggle.setAttribute('aria-controls', settings.id);
        toggle.setAttribute('aria-describedby', status.id);
        toggle.setAttribute('aria-expanded', String(!settings.hidden));
        toggle.addEventListener('click', () => {
            settings.hidden = !settings.hidden;
            toggle.setAttribute('aria-expanded', String(!settings.hidden));
            if (settings.hidden) expandedOverlays.delete(overlay);
            else expandedOverlays.add(overlay);
        });
        settings.append(status, styleControls(overlay.featureId, overlay.settings, () => { persist(); updateStatuses(); }));
        const actions = node('div', 'overlay-actions');
        const edit = node('button', '', 'Change source…');
        edit.type = 'button';
        edit.addEventListener('click', () => openEditor(overlay, edit));
        const remove = node('button', 'overlay-remove');
        remove.type = 'button';
        remove.title = 'Remove overlay';
        remove.setAttribute('aria-label', `Remove ${featureLabel} overlay`);
        const removeIcon = node('i', 'fa-solid fa-trash-can');
        removeIcon.setAttribute('aria-hidden', 'true');
        remove.append(removeIcon);
        remove.addEventListener('click', () => {
            expandedOverlays.delete(overlay);
            overlays.splice(overlays.indexOf(overlay), 1);
            persist();
            renderList();
            addButton.focus();
        });
        header.append(visibility, toggle, remove);
        actions.append(edit);
        settings.append(actions);
        const metadata = node('details', 'overlay-details');
        metadata.hidden = !overlay.selection.primary;
        if (overlay.selection.primary) metadata.append(node('summary', '', 'Details'), technicalDetails(overlay.selection.primary));
        settings.append(metadata);
        card.append(header, settings);
        cardNodes.set(overlay, {card, toggle, title, checkbox, remove, status, metadata, featureLabel});
        list.append(card);
    }
    updateStatuses();
    updateSourceLabels();
}

function updateStatuses() {
    for (const [overlay, row] of cardNodes) {
        const resolved = resolveOverlay(frameIndex, overlay);
        const state = overlayStatus(frameIndex, overlay, knownOutputs, resolved);
        if (row.status.textContent !== state) row.status.textContent = state;
        const available = !['waiting', 'unsupported'].includes(resolved.state);
        const color = available && /^#[0-9a-f]{6}$/i.test(overlay.settings.color) ? overlay.settings.color : available ? '#ffffff' : '#d1d5db';
        if (row.color !== color || row.available !== available) {
            row.card.style.setProperty('--overlay-color', color);
            row.card.style.setProperty('--overlay-foreground', available ? contrastingText(color) : '#4b5563');
            row.card.setAttribute('data-unavailable', String(!available));
            row.color = color;
            row.available = available;
        }
        const tooltip = `${row.title.textContent}\n${state}`;
        if (row.toggle.title !== tooltip) row.toggle.title = tooltip;
    }
}

// Keep titles readable even when the chosen overlay color is very dark or light.
function contrastingText(color) {
    const [r, g, b] = [1, 3, 5].map((offset) => {
        const channel = parseInt(color.slice(offset, offset + 2), 16) / 255;
        return channel <= 0.04045 ? channel / 12.92 : ((channel + 0.055) / 1.055) ** 2.4;
    });
    return 0.2126 * r + 0.7152 * g + 0.0722 * b > 0.179 ? '#000000' : '#ffffff';
}

function availableDescriptors() {
    const descriptors = new Map(knownOutputs);
    // A saved selection remains editable even while its source is absent.
    if (editing?.selection.primary) descriptors.set(selectionKey(editing), editing.selection.primary);
    // Discovery/reconnect must not silently rebind an in-progress selection.
    if (draft?.selection.primary) descriptors.set(selectionKey(draft), draft.selection.primary);
    return descriptors;
}

function refreshFeatures() {
    const previous = featureSelect.value;
    const catalog = availableDescriptors();
    const features = new Map(OVERLAY_FEATURES.filter((feature) => feature.render || feature.discover(catalog).length)
        .map((feature) => [feature.id, feature.label]));
    for (const overlay of [editing, draft]) {
        if (overlay && !features.has(overlay.featureId)) features.set(overlay.featureId, featureFor(overlay.featureId)?.label || overlay.featureId);
    }
    featureSelect.replaceChildren();
    for (const [id, label] of features) featureSelect.add(new Option(label, id));
    if (!features.size) featureSelect.add(new Option('Waiting for pipeline outputs', ''));
    if (features.has(previous)) featureSelect.value = previous;
    featureSelect.disabled = !features.size;
}

function refreshOutputs() {
    const previous = outputSelect.value;
    const catalog = availableDescriptors();
    const sources = featureFor(featureSelect.value)?.discover(catalog) || [];
    const choices = new Map(sources.map((source) => [sourceKey(source), source]));
    for (const overlay of [editing, draft]) {
        if (overlay?.featureId === featureSelect.value && overlay.selection.primary) choices.set(selectionKey(overlay), overlay.selection.primary);
    }
    const descriptors = [...choices.values()];
    const labels = sourceLabels(descriptors, false);
    outputSelect.replaceChildren();
    const hasChoice = descriptors.length > 1;
    const waiting = (draft?.featureId === featureSelect.value && !draft.selection.primary)
        || (!draft && editing?.featureId === featureSelect.value && !editing.selection.primary);
    if (hasChoice && (waiting || !previous)) outputSelect.add(new Option('Choose a source', ''));
    for (const source of descriptors) {
        const key = sourceKey(source);
        outputSelect.add(new Option(labels.get(key), key));
    }
    if ([...outputSelect.options].some((option) => option.value === previous)) outputSelect.value = previous;
    sourceField.hidden = !hasChoice;
    outputSelect.disabled = !hasChoice;
    outputSelect.required = hasChoice;
    sourceSummary.hidden = hasChoice;
    sourceSummary.textContent = descriptors.length === 1
        ? `From ${labels.get(sourceKey(descriptors[0]))}` : 'Source will be selected when available.';
    updateDraft();
}

function updateDraft() {
    const source = availableDescriptors().get(outputSelect.value) || null;
    if (!featureSelect.value) {
        draft = null;
        editorSettings.replaceChildren();
        editorDetails.hidden = true;
        editorHint.textContent = 'Choose what to show.';
        submitButton.disabled = true;
        return;
    }
    const key = source ? sourceKey(source) : '';
    if (!draft || draft.featureId !== featureSelect.value || selectionKey(draft) !== key) {
        const previousDraft = draft;
        draft = createOverlay(featureSelect.value, source);
        draft.enabled = editing?.enabled ?? true;
        if (previousDraft?.featureId === draft.featureId && !previousDraft.selection.primary) {
            draft.settings = {...previousDraft.settings};
        } else if (editing?.featureId === draft.featureId && selectionKey(editing) === key) {
            draft.settings = {...editing.settings};
        }
        editorSettings.replaceChildren(styleControls(draft.featureId, draft.settings, updateDraft));
        editorDetails.replaceChildren(node('summary', '', 'Details'));
        if (source) editorDetails.append(technicalDetails(source));
    }
    editorDetails.hidden = !source;
    const resolved = resolveOverlay(frameIndex, draft);
    const needsChoice = !source && !outputSelect.disabled;
    const hint = needsChoice ? 'Choose a source.' : resolved.state === 'ready' ? '' : resolved.message;
    if (editorHint.textContent !== hint) editorHint.textContent = hint;
    submitButton.disabled = resolved.state === 'unsupported' || needsChoice;
}

function openEditor(overlay, opener) {
    editing = overlay;
    draft = null;
    editorDetails.open = false;
    returnFocus = opener;
    document.getElementById('overlayDialogTitle').textContent = overlay ? 'Edit overlay' : 'Add overlay';
    submitButton.textContent = overlay ? 'Apply' : 'Add';
    refreshFeatures();
    if (overlay) featureSelect.value = overlay.featureId;
    refreshOutputs();
    if (overlay?.selection.primary) {
        outputSelect.value = selectionKey(overlay);
        updateDraft();
    }
    dialog.showModal();
}

addButton.addEventListener('click', () => openEditor(null, addButton));
featureSelect.addEventListener('change', refreshOutputs);
outputSelect.addEventListener('change', updateDraft);
document.getElementById('cancelOverlayBtn').addEventListener('click', () => dialog.close());
dialog.addEventListener('close', () => {
    editing = null;
    draft = null;
    if (returnFocus.isConnected) returnFocus.focus();
    else addButton.focus();
});
form.addEventListener('submit', (event) => {
    event.preventDefault();
    if (!draft || submitButton.disabled || resolveOverlay(frameIndex, draft).state === 'unsupported') return;
    if (editing) Object.assign(editing, draft);
    else overlays.push(draft);
    persist();
    renderList();
    dialog.close();
});

function updateMetadata(frame, decodeError) {
    frameIndex = indexFrame(frame);
    connection.textContent = decodeError
        ? `Metadata unavailable: ${decodeError}`
        : frame ? '' : 'Waiting for pipeline outputs';
    let changed = false;
    if (decodeError) {
        knownOutputs.clear();
        changed = true;
    }
    for (const [key, output] of frameIndex.outputs) {
        if (!knownOutputs.has(key)) {
            knownOutputs.set(key, output.source);
            changed = true;
        }
    }
    const bound = bindPendingOverlays(overlays, knownOutputs);
    if (bound) persist();
    updateStatuses();
    if (changed || bound) updateSourceLabels();
    if (dialog.open) {
        if (changed) {
            refreshFeatures();
            refreshOutputs();
        } else updateDraft();
    }
}

window.addEventListener('metadata-message', (event) => {
    updateMetadata(event.detail?.frame_results || null, event.detail?.decode_error);
});

window.addEventListener('video-frame-results', (event) => {
    if (event.detail) updateMetadata(event.detail);
    displayedFrameIndex = event.detail ? frameIndex : indexFrame(null);
    // Draw synchronously inside requestVideoFrameCallback, including null clears.
    renderOsd(canvas, video, displayedFrameIndex, overlays);
});

window.addEventListener('metadata-connection', (event) => {
    if (event.detail.connected) return;
    frameIndex = indexFrame(null);
    knownOutputs.clear();
    connection.textContent = 'Metadata disconnected. Waiting for pipeline outputs.';
    updateStatuses();
    updateSourceLabels();
    if (dialog.open) {
        refreshFeatures();
        refreshOutputs();
    }
});

function renderFrame() {
    renderOsd(canvas, video, displayedFrameIndex, overlays);
    window.requestAnimationFrame(renderFrame);
}

renderList();
window.requestAnimationFrame(renderFrame);
