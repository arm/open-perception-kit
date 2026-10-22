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
import {loadLocalImage} from './overlay-image.js';

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
let nextControlId = 0;

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

function styleControls(featureId, settings, changed, preview = changed, controls = featureFor(featureId)?.controls || []) {
    const container = node('div', 'overlay-style-controls');
    for (const control of controls) {
        if (control.type === 'checkbox') {
            const label = node('label', control.switch ? 'model-toggle-switch overlay-visible' : 'overlay-visibility');
            const input = document.createElement('input');
            input.type = 'checkbox';
            input.checked = settings[control.key];
            if (control.ariaLabel) input.setAttribute('aria-label', control.ariaLabel);
            input.addEventListener('change', () => {
                settings[control.key] = input.checked;
                changed();
            });
            if (control.switch) {
                input.setAttribute('role', 'switch');
                const track = node('span', 'model-toggle-track');
                track.setAttribute('aria-hidden', 'true');
                track.append(node('span', 'model-toggle-thumb'));
                label.append(input, track);
            } else label.append(input, node('span', 'wfr-txt-label', control.label));
            container.append(label);
            continue;
        }
        const label = node('label', 'overlay-field');
        label.append(node('span', 'wfr-txt-label', control.label));
        if (control.type === 'image') {
            const input = document.createElement('input');
            input.type = 'file';
            input.accept = 'image/png,image/jpeg,image/webp';
            input.setAttribute('aria-label', control.label);
            const status = node('output', 'overlay-hint wfr-txt-caption', settings[control.key] ? 'Image saved in this browser.' : 'No image selected.');
            status.setAttribute('role', 'status');
            status.id = `overlay-image-${++nextControlId}`;
            input.setAttribute('aria-describedby', status.id);
            const clear = node('button', '', 'Clear image');
            clear.type = 'button';
            let revision = 0;
            clear.addEventListener('click', () => {
                revision++;
                input.value = '';
                input.setCustomValidity('');
                input.setAttribute('aria-invalid', 'false');
                status.setAttribute('data-state', 'info');
                settings[control.key] = '';
                status.textContent = 'No image selected.';
                changed();
            });
            input.addEventListener('change', async () => {
                const file = input.files[0];
                if (!file) return;
                const current = ++revision;
                input.setCustomValidity('Wait for the image to finish loading.');
                input.setAttribute('aria-invalid', 'false');
                status.setAttribute('data-state', 'info');
                status.textContent = 'Preparing image…';
                try {
                    const image = await loadLocalImage(file);
                    if (current !== revision || !input.isConnected) return;
                    settings[control.key] = image;
                    status.textContent = 'Image prepared locally; never uploaded.';
                    changed();
                } catch (error) {
                    if (current === revision) {
                        status.textContent = error.message;
                        status.setAttribute('data-state', 'danger');
                        input.setAttribute('aria-invalid', 'true');
                    }
                } finally {
                    if (current === revision) input.setCustomValidity('');
                }
            });
            label.append(input, status);
            container.append(label, clear);
            continue;
        }
        if (control.type === 'select') {
            const input = document.createElement('select');
            input.setAttribute('aria-label', control.label);
            for (const option of control.options) input.add(new Option(option.label, option.value));
            input.value = settings[control.key];
            input.addEventListener('change', () => { settings[control.key] = input.value; changed(); });
            label.append(input);
            container.append(label);
            continue;
        }
        const row = node('span', 'overlay-style-value');
        const input = document.createElement('input');
        input.type = control.type;
        input.setAttribute('aria-label', control.ariaLabel || control.label);
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
            preview();
        });
        input.addEventListener('change', changed);
        updateValue();
        row.append(input, value);
        label.append(row);
        container.append(label);
    }
    return container;
}

function technicalDetails(source) {
    const description = node('dl', 'overlay-metadata wfr-txt-code--sm');
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
        const visibility = node('label', 'model-toggle-switch overlay-visible');
        const checkbox = document.createElement('input');
        checkbox.type = 'checkbox';
        checkbox.setAttribute('role', 'switch');
        checkbox.checked = overlay.enabled;
        checkbox.addEventListener('change', () => {
            overlay.enabled = checkbox.checked;
            persist();
            updateStatuses();
            updateLegends();
        });
        const track = node('span', 'model-toggle-track');
        track.setAttribute('aria-hidden', 'true');
        track.append(node('span', 'model-toggle-thumb'));
        visibility.append(checkbox, track);
        const toggle = node('button', 'overlay-toggle');
        toggle.type = 'button';
        toggle.id = `overlay-toggle-${index}`;
        const chevron = node('span', 'icon');
        chevron.setAttribute('data-icon', 'chevron-right');
        chevron.setAttribute('aria-hidden', 'true');
        const swatch = node('span', 'overlay-swatch');
        swatch.setAttribute('aria-hidden', 'true');
        swatch.hidden = !Object.hasOwn(overlay.settings, 'color');
        const title = node('span', 'overlay-title wfr-txt-label');
        toggle.append(swatch, title, chevron);
        const status = node('p', 'overlay-hint wfr-txt-caption');
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
        settings.append(status, styleControls(overlay.featureId, overlay.settings,
            () => { persist(); updateStatuses(); }, updateStatuses));
        const legend = featureFor(overlay.featureId)?.legend ? node('section', 'overlay-legend') : null;
        if (legend) {
            legend.setAttribute('aria-label', 'Class legend');
            settings.append(legend);
        }
        const actions = node('div', 'overlay-actions');
        const edit = node('button', '', 'Change source…');
        edit.type = 'button';
        edit.addEventListener('click', () => openEditor(overlay, edit));
        const remove = node('button', 'overlay-remove');
        remove.type = 'button';
        remove.title = 'Remove overlay';
        remove.setAttribute('aria-label', `Remove ${featureLabel} overlay`);
        const removeIcon = node('span', 'icon');
        removeIcon.setAttribute('data-icon', 'trash-2');
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
        cardNodes.set(overlay, {card, toggle, title, checkbox, remove, status, metadata, featureLabel, legend});
        list.append(card);
    }
    updateStatuses();
    updateSourceLabels();
    updateLegends();
}

// Legends follow the displayed frame, not the independent live metadata channel.
// Keep class controls alive across frames so discovery never interrupts an edit.
function updateLegends() {
    for (const [overlay, row] of cardNodes) {
        if (!row.legend) continue;
        const feature = featureFor(overlay.featureId);
        const resolved = !overlay.enabled ? {message: 'Overlay hidden.'}
            : !displayedFrameIndex.frame ? {message: 'Waiting for matching video metadata.'}
                : resolveOverlay(displayedFrameIndex, overlay);
        const legend = feature.legend(resolved, overlay.settings);
        if (!row.legendNodes) {
            row.legendNodes = new Map();
            row.legendMessage = node('p', 'overlay-hint wfr-txt-caption');
            row.legendList = node('ul', 'overlay-legend-list');
            const header = node('div', 'overlay-legend-header');
            header.append(node('p', 'wfr-txt-label', 'Class legend'));
            if (feature.classControls) {
                const reset = node('button', 'wfr-txt-caption', 'Reset labels');
                reset.type = 'button';
                reset.addEventListener('click', () => {
                    overlay.settings.classes = Object.fromEntries(Object.entries(overlay.settings.classes)
                        .map(([id, {label, ...style}]) => [id, style]));
                    for (const item of row.legendNodes.values()) item.resetLabel?.();
                    persist();
                    updateLegends();
                });
                header.append(reset);
            }
            row.legend.replaceChildren(header, row.legendMessage, row.legendList);
            if (feature.classControls) row.legend.append(node('p', 'overlay-hint wfr-txt-caption',
                'Class opacity is multiplied by the overlay opacity. Hidden classes reveal the video.'));
        }
        if (row.legendMessage.textContent !== legend.message) row.legendMessage.textContent = legend.message;
        const present = new Set(legend.entries.filter(entry => entry.present).map(entry => entry.id));
        for (const entry of legend.entries) {
            let item = row.legendNodes.get(entry.id);
            if (!item) {
                const swatch = node('span', 'overlay-swatch');
                swatch.setAttribute('aria-hidden', 'true');
                const element = node('li', 'wfr-txt-caption');
                const heading = node('div', 'overlay-class-heading');
                const presence = node('span', 'overlay-class-presence');
                const name = node(feature.classControls ? 'input' : 'span', 'overlay-class-name');
                heading.append(swatch, name, presence);
                element.append(heading);
                let resetLabel;
                if (feature.classControls) {
                    element.className += ' overlay-class-row';
                    const settings = {enabled: entry.enabled, color: entry.color, opacity: entry.opacity};
                    if (overlay.settings.classes?.[entry.id]?.label) settings.label = overlay.settings.classes[entry.id].label;
                    const preview = () => {
                        const style = {...settings};
                        if (style.label?.trim()) style.label = style.label.trim();
                        else delete style.label;
                        overlay.settings.classes = {...overlay.settings.classes, [entry.id]: style};
                        updateLegends();
                    };
                    name.type = 'text';
                    name.maxLength = 80;
                    name.placeholder = `Class ${entry.id}`;
                    name.value = settings.label || '';
                    resetLabel = () => { delete settings.label; name.value = ''; };
                    name.setAttribute('aria-label', `Class ${entry.id} name`);
                    name.addEventListener('input', () => { settings.label = name.value; preview(); });
                    name.addEventListener('change', () => {
                        name.value = settings.label?.trim() || '';
                        settings.label = name.value;
                        persist();
                    });
                    name.addEventListener('keydown', event => {
                        if (event.key === 'Enter') { event.preventDefault(); name.blur(); }
                    });
                    const controls = feature.classControls.map(control => ({...control,
                        ariaLabel: `Class ${entry.id} (${entry.id}) ${control.label.toLowerCase()}`}));
                    const inputs = styleControls(overlay.featureId, settings, () => { preview(); persist(); }, preview, controls);
                    inputs.className += ' overlay-class-controls';
                    element.append(inputs);
                } else name.textContent = entry.label;
                const nextId = [...row.legendNodes.keys()].sort((a, b) => a - b).find(id => id > entry.id);
                row.legendList.insertBefore(element, row.legendNodes.get(nextId)?.element || null);
                item = {element, swatch, presence, heading, resetLabel, label: `${entry.id} · ${entry.label}`};
                row.legendNodes.set(entry.id, item);
            }
            item.label = `${entry.id} · ${entry.label}`;
            if (item.color !== entry.color) {
                item.swatch.style.setProperty('--overlay-color', entry.color);
                item.color = entry.color;
            }
        }
        for (const [id, item] of row.legendNodes) {
            const text = present.has(id) ? 'In frame' : 'Not in frame';
            if (item.presence.textContent !== text) {
                item.presence.textContent = text;
            }
            const title = `${item.label} · ${text}`;
            if (item.heading.title !== title) item.heading.title = title;
        }
    }
}

function updateStatuses() {
    for (const [overlay, row] of cardNodes) {
        const resolved = resolveOverlay(frameIndex, overlay);
        const state = overlayStatus(frameIndex, overlay, knownOutputs, resolved);
        if (row.status.textContent !== state) row.status.textContent = state;
        const available = !['waiting', 'unsupported'].includes(resolved.state);
        // This is visualization data, used only by the swatch, not UI surfaces.
        const color = /^#[0-9a-f]{6}$/i.test(overlay.settings.color) ? overlay.settings.color : 'transparent';
        if (row.color !== color || row.available !== available) {
            row.card.style.setProperty('--overlay-color', color);
            row.card.setAttribute('data-unavailable', String(!available));
            row.color = color;
            row.available = available;
        }
        const tooltip = `${row.title.textContent}\n${state}`;
        if (row.toggle.title !== tooltip) row.toggle.title = tooltip;
    }
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

function refreshOutputs(previous = outputSelect.value) {
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
    if (hasChoice && (waiting || !choices.has(previous))) outputSelect.add(new Option('Choose a source', ''));
    for (const source of descriptors) {
        const key = sourceKey(source);
        outputSelect.add(new Option(labels.get(key), key));
    }
    if (choices.has(previous)) outputSelect.value = previous;
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
            draft.settings = previousDraft.settings;
        } else if (editing?.featureId === draft.featureId && selectionKey(editing) === key) {
            draft.settings = {...editing.settings};
        }
        // Discovery may bind a waiting source while a local image is decoding.
        // Keep its controls and settings alive instead of abandoning that upload.
        if (draft.settings !== previousDraft?.settings) {
            editorSettings.replaceChildren(styleControls(draft.featureId, draft.settings, updateDraft));
        }
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
    // Start from this overlay's saved choice, never the previous dialog's value.
    refreshOutputs(selectionKey(overlay));
    dialog.showModal();
}

addButton.addEventListener('click', () => openEditor(null, addButton));
featureSelect.addEventListener('change', () => refreshOutputs());
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
    updateLegends();
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
if (typeof video.requestVideoFrameCallback !== 'function') {
    console.warn('Synchronized overlays require requestVideoFrameCallback; overlays are disabled.');
}
window.requestAnimationFrame(renderFrame);
