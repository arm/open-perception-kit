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

import {defineFeature} from '../overlay-feature.js';
import {drawTrackTrace} from '../overlay-drawing.js';
import {trackKey, traceMatchesCurrentTrack} from '../tracking-data.js';

export default defineFeature({
    id: 'movement-trails', label: 'Movement trails',
    defaults: {color: '#ffff00'},
    accepts: (source) => source.payloadType === 'TrackTraces',
    resolve(result, index) {
        const items = result.items.filter(item => {
            const entries = index.byTrack.get(trackKey(item.data.identity)) || [];
            return item.type === 'TrackTrace' && item.data.trackingValid === true
                && entries.filter(entry => entry.type === 'TrackTrace').length === 1
                && traceMatchesCurrentTrack(item.data, entries.filter(entry => entry.source.payloadType === 'ObjectTracks'));
        });
        return items.length === result.items.length ? {...result, items} : {...result, items, state: 'partial', message: 'Invalid or ambiguous trace omitted.'};
    },
    render({ctx, mapper}, resolved, settings) {
        for (const item of resolved.items) {
            if (item.type === 'TrackTrace') drawTrackTrace(ctx, item.data, mapper, settings.color);
        }
    },
});
