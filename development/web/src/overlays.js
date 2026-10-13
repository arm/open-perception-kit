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
import {sourceKey, validSource} from './overlay-sources.js';

export const OVERLAY_STORAGE_KEY = 'opk-overlays:v2';
const LEGACY_STORAGE_KEY = 'opk-overlays:v1';

function legacyFeature(source) {
    if (source.payloadType === 'Classifications' && source.contentType === 'cameraContact') return 'camera-contact';
    return {
        BoxDetections: 'bounding-boxes', ObjectTracks: 'tracked-objects', TrackTraces: 'movement-trails',
        Classifications: 'classifications', PoseEstimations: 'gaze', PerformanceOverlay: 'performance',
    }[source.payloadType] || 'unsupported';
}

function validLegacy(overlay) {
    return validSource(overlay?.source) && typeof overlay.enabled === 'boolean'
        && typeof overlay.settings?.color === 'string' && /^#[0-9a-f]{6}$/i.test(overlay.settings.color)
        && Number.isFinite(overlay.settings?.opacity) && overlay.settings.opacity >= 0 && overlay.settings.opacity <= 1;
}

function migrateLegacy(overlay, saved) {
    if (!validLegacy(overlay)) return null;
    const featureId = legacyFeature(overlay.source);
    const settings = {...overlay.settings};
    if (featureId === 'tracked-objects') {
        // Preserve the old explicit-trail override as an owned checkbox value,
        // once during migration. Features never consult other overlay settings.
        const oldTraceKey = sourceKey({...overlay.source, payloadType: 'TrackTraces', contentType: 'trackTrace'});
        settings.boxes = true;
        settings.trails = !saved.some((entry) => validLegacy(entry) && sourceKey(entry.source) === oldTraceKey);
    }
    return {featureId, selection: {primary: overlay.source}, enabled: overlay.enabled, settings};
}

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
    return {featureId: overlay.featureId, selection: {primary: primary === null ? null : {...primary}}, enabled: overlay.enabled, settings};
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
        if (!feature?.render) continue;
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
        const raw = current ?? storage.getItem(LEGACY_STORAGE_KEY);
        if (raw === null) return {overlays: [], notice: ''};
        const saved = JSON.parse(raw);
        const migrating = current === null;
        if (saved?.version !== (migrating ? 1 : 2) || !Array.isArray(saved.overlays)) throw new Error('Unsupported settings');
        const candidates = migrating ? saved.overlays.map((entry) => migrateLegacy(entry, saved.overlays)) : saved.overlays;
        const overlays = candidates.map(normalizeOverlay).filter(Boolean);
        let relabelled = false;
        for (const overlay of overlays) {
            const source = overlay.selection.primary;
            // Older bounding-box selections rendered humanFace regions as circles.
            // Keep that appearance and selector, but give it an honest feature name.
            if (overlay.featureId === 'bounding-boxes' && source?.contentType === 'humanFace'
                && ['BoxDetections', 'ObjectTracks'].includes(source.payloadType)) {
                overlay.featureId = 'face-circles';
                relabelled = true;
            }
        }
        const notices = [];
        if (overlays.length !== candidates.length) notices.push('Some invalid saved overlays were skipped.');
        // Keep the v1 record as a backup. Failed migration writes can be retried
        // on reload; never delete the only persisted copy of the user's choices.
        if (migrating || relabelled) notices.push(saveOverlays(storage, overlays));
        return {overlays, notice: notices.filter(Boolean).join(' ')};
    } catch {
        return {overlays: [], notice: 'Saved overlays could not be loaded. You can still add overlays.'};
    }
}

export function saveOverlays(storage, overlays) {
    try {
        const saved = overlays.map(normalizeOverlay);
        if (saved.some((overlay) => !overlay)) throw new Error('Invalid overlay settings');
        storage.setItem(OVERLAY_STORAGE_KEY, JSON.stringify({version: 2, overlays: saved}));
        return '';
    } catch {
        return 'Overlay settings could not be saved. Changes apply only to this page session.';
    }
}
