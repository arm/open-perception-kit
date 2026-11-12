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

import {defaultSettings, featureFor} from './overlay-features.js';
import {copySource, validSource} from './overlay-sources.js';

export const OVERLAY_STORAGE_KEY = 'opk-overlays:v3';
const PREVIOUS_STORAGE_KEY = 'opk-overlays:v2';

function normalizeOverlay(overlay) {
    const primary = overlay?.selection?.primary;
    if (!overlay || typeof overlay.featureId !== 'string' || !overlay.featureId
        || typeof overlay.enabled !== 'boolean' || (primary !== null && !validSource(primary))) return null;
    const feature = featureFor(overlay.featureId);
    // Unknown saved features remain visible as unsupported, without losing their
    // selection or settings when another overlay is edited.
    if (!overlay.settings || typeof overlay.settings !== 'object' || Array.isArray(overlay.settings)) return null;
    const settings = feature ? feature.validateSettings(overlay.settings) : overlay.settings;
    if (!settings) return null;
    return {featureId: overlay.featureId, selection: {primary: primary === null ? null : copySource(primary)}, enabled: overlay.enabled, settings};
}

export function createOverlay(featureId, source = null) {
    return {featureId, selection: {primary: source}, enabled: true, settings: defaultSettings(featureId, source)};
}

// Bind a waiting overlay only once, using real observed provenance. Once bound,
// disconnects and later discoveries cannot redirect it to a different producer.
export function bindPendingOverlays(overlays, catalog) {
    let changed = false;
    for (const overlay of overlays) {
        if (overlay.selection.primary !== null) continue;
        const feature = featureFor(overlay.featureId);
        if (!feature?.render || feature.requiresExplicitSource) continue;
        const sources = feature.discover(catalog);
        if (sources.length !== 1) continue;
        overlay.selection.primary = sources[0];
        changed = true;
    }
    return changed;
}

export function loadOverlays(storage) {
    try {
        const current = storage.getItem(OVERLAY_STORAGE_KEY);
        const raw = current ?? storage.getItem(PREVIOUS_STORAGE_KEY);
        if (raw === null) return {overlays: [], notice: ''};
        const saved = JSON.parse(raw);
        const migrating = current === null;
        if (saved?.version !== (migrating ? 2 : 3) || !Array.isArray(saved.overlays)) throw new Error('Unsupported settings');
        // Preserve explicit renderer/settings/provenance, dropping retired selector fields.
        // Never infer a payload family or rendering style from old semantic labels.
        const candidates = saved.overlays;
        const overlays = candidates.map(normalizeOverlay).filter(Boolean);
        const notices = [];
        if (overlays.length !== candidates.length) notices.push('Some invalid saved overlays were skipped.');
        // Keep the previous record as a backup. Failed migration writes can be retried
        // on reload; never delete the only persisted copy of the user's choices.
        if (migrating) {
            notices.push('Saved selections migrated. Marker shape is now an explicit display setting.');
            notices.push(saveOverlays(storage, overlays));
        }
        return {overlays, notice: notices.filter(Boolean).join(' ')};
    } catch {
        return {overlays: [], notice: 'Saved overlays could not be loaded. You can still add overlays.'};
    }
}

export function saveOverlays(storage, overlays) {
    try {
        const saved = overlays.map(normalizeOverlay);
        if (saved.some((overlay) => !overlay)) throw new Error('Invalid overlay settings');
        storage.setItem(OVERLAY_STORAGE_KEY, JSON.stringify({version: 3, overlays: saved}));
        return '';
    } catch {
        return 'Overlay settings could not be saved. Changes apply only to this page session.';
    }
}
