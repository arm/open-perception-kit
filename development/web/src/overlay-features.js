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

export const OVERLAY_FEATURES = [
];

export function featureFor(id) {
    return OVERLAY_FEATURES.find((feature) => feature.id === id);
}

export function defaultSettings(id, source) {
    const defaults = featureFor(id)?.defaults || {color: '#ffffff', opacity: 1};
    return typeof defaults === 'function' ? defaults(source || {}) : {...defaults};
}

export function resolveOverlay(index, overlay) {
    const feature = featureFor(overlay.featureId);
    if (!feature) return {state: 'unsupported', message: 'Unknown rendering feature.'};
    if (overlay.selection.primary === null && feature.render) return {state: 'waiting', message: 'Waiting for source'};
    const settings = {...defaultSettings(feature.id, overlay.selection.primary), ...overlay.settings};
    return {...feature.resolve(index, overlay.selection, settings), feature, settings};
}

export function overlayStatus(index, overlay, catalog, resolved = resolveOverlay(index, overlay)) {
    if (overlay.selection.primary === null && catalog && featureFor(overlay.featureId)?.discover(catalog).length > 1) {
        return 'Choose a source';
    }
    if (resolved.state === 'unsupported') return `Unsupported · ${resolved.message}`;
    if (resolved.state === 'waiting') return 'Waiting for source';
    if (!overlay.enabled) return 'Hidden';
    if (resolved.state === 'partial') return `Partial · ${resolved.message}`;
    if (resolved.state === 'ambiguous') return `Ambiguous · ${resolved.message}`;
    return resolved.items.length ? 'Ready' : 'Ready · no results';
}
