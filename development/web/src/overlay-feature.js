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

import {objectId, sourceKey} from './overlay-sources.js';
import {validLocalImage} from './overlay-image.js';

export const STYLE_CONTROLS = [
    {key: 'color', label: 'Color', type: 'color', value: '#ffffff'},
    {key: 'opacity', label: 'Opacity', type: 'range', min: 0, max: 1, step: 0.01, value: 1},
];
const CONTENT_COLORS = {
    humanFace: '#2600ff', genericObject: '#ff3030', eyeYawPitch: '#fafad2',
    cameraContact: '#66ff00', personClassification: '#66ff00', trackTrace: '#ffff00',
};

// Shared source lookup, palette and validation. Plugins own their accepted
// sources, optional default overrides, relationship resolver and drawing function.
export function defineFeature({controls = STYLE_CONTROLS, defaults = {}, resolve = (result) => result, reason, ...definition}) {
    return {
        ...definition,
        controls,
        defaults(source) {
            const settings = Object.fromEntries(controls.map((control) => [control.key, control.value]));
            if (Object.hasOwn(settings, 'color')) settings.color = CONTENT_COLORS[source.contentType] || settings.color;
            return {...settings,
                ...(typeof defaults === 'function' ? defaults(source) : defaults)};
        },
        discover(catalog) { return [...catalog.values()].filter(definition.accepts); },
        validateSettings(settings) {
            if (!settings || typeof settings !== 'object' || Array.isArray(settings)) return null;
            const result = {};
            for (const control of controls) {
                const value = Object.hasOwn(settings, control.key) ? settings[control.key] : control.value;
                const valid = control.type === 'checkbox' ? typeof value === 'boolean'
                    : control.type === 'color' ? typeof value === 'string' && /^#[0-9a-f]{6}$/i.test(value)
                        : control.type === 'select' ? control.options.some((option) => option.value === value)
                            : control.type === 'image' ? validLocalImage(value)
                                : Number.isFinite(value) && value >= control.min && value <= control.max;
                if (!valid) return null;
                result[control.key] = value;
            }
            return result;
        },
        resolve(index, selection, settings) {
            if (!selection?.primary || !definition.accepts(selection.primary)) {
                return {state: 'unsupported', message: 'This source cannot provide the selected feature.'};
            }
            if (reason) return {state: 'unsupported', message: reason};
            const output = index.outputs.get(sourceKey(selection.primary));
            if (!output) return {state: 'waiting', message: 'Waiting for source'};
            return resolve({state: 'ready', message: '', output, items: output.items}, index, settings);
        },
    };
}

export function resolveFaceParents(result, index, type) {
    const items = result.items.filter((item) => item.type === type);
    const resolved = {...result, items, attached: []};
    let missing = false;
    let ambiguous = false;
    for (const item of items) {
        const candidates = (index.objects.get(objectId(item.data.parentUuid)) || []).filter((candidate) =>
            ['BoxDetections', 'ObjectTracks'].includes(candidate.source.payloadType)
            && candidate.source.contentType === 'humanFace' && candidate.type === 'Rect');
        if (candidates.length > 1) ambiguous = true;
        else if (candidates.length === 1) resolved.attached.push({data: item.data, parent: candidates[0].data});
        else missing = true;
    }
    if (ambiguous) return {...resolved, state: 'ambiguous', message: 'Multiple parent faces match; ambiguous markers are omitted.'};
    if (missing) return {...resolved, state: 'partial', message: 'Waiting for parent face geometry.'};
    return resolved;
}
