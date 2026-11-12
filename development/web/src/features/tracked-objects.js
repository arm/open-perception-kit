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

import {defineFeature, STYLE_CONTROLS} from '../overlay-feature.js';
import {trackKey} from '../overlay-sources.js';
import {traceMatchesCurrentTrack} from '../tracking-data.js';
import {drawFace, drawObjectBox, drawTrackTrace} from '../overlay-drawing.js';

export default defineFeature({
    id: 'tracked-objects', label: 'Tracked objects',
    defaults: {color: '#ff3030'},
    accepts: (source) => source.payloadType === 'ObjectTracks',
    controls: [
        {key: 'boxes', label: 'Boxes', type: 'checkbox', value: true},
        {key: 'trails', label: 'Trails', type: 'checkbox', value: true},
        {key: 'shape', label: 'Marker shape', type: 'select', value: 'rectangle',
            options: [{value: 'rectangle', label: 'Rectangle'}, {value: 'circle', label: 'Circle'}]},
        ...STYLE_CONTROLS,
    ],
    resolve(result, index, settings) {
        const items = result.items.filter((item) => item.type === 'Rect' && item.data.trackingValid === true && trackKey(item.data.identity));
        const resolved = {...result, items, trails: []};
        if (!items.length && result.items.length) return {...resolved, state: 'partial', message: 'Tracking identity or sample unavailable.'};
        if (!settings.trails || !items.length) return resolved;
        let missing = false;
        let ambiguous = false;
        for (const key of new Set(items.map((item) => trackKey(item.data.identity)))) {
            const entries = index.byTrack.get(key) || [];
            const tracks = entries.filter(item => item.source.payloadType === 'ObjectTracks');
            const candidates = entries.filter((item) => item.source.payloadType === 'TrackTraces' && item.type === 'TrackTrace');
            if (tracks.length > 1) { ambiguous = true; continue; }
            if (candidates.length > 1) ambiguous = true;
            else if (candidates.length === 1 && candidates[0].data.trackingValid === true && candidates[0].data.points?.length >= 2
                && traceMatchesCurrentTrack(candidates[0].data, tracks)) resolved.trails.push(candidates[0].data);
            else missing = true;
        }
        if (ambiguous) return {...resolved, state: 'ambiguous', message: 'Multiple trails match a track; ambiguous trails are omitted.'};
        if (missing) return {...resolved, state: 'partial', message: 'Waiting for matching trail history.'};
        return resolved;
    },
    render({ctx, mapper}, resolved, settings) {
        if (settings.trails) {
            for (const trace of resolved.trails) drawTrackTrace(ctx, trace, mapper, settings.color);
        }
        if (settings.boxes) {
            const draw = settings.shape === 'circle' ? drawFace : drawObjectBox;
            for (const {data} of resolved.items) draw(ctx, data, mapper, settings.color);
        }
    },
});
