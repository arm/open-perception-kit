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

import {createCoordinateMapper} from './overlay-drawing.js';
import {featureFor, resolveOverlay} from './overlay-features.js';

export function renderOsd(canvas, video, index, overlays = [], {now = Date.now()} = {}) {
    const dpr = window.devicePixelRatio || 1;
    const pixelWidth = Math.max(1, Math.round(canvas.clientWidth * dpr));
    const pixelHeight = Math.max(1, Math.round(canvas.clientHeight * dpr));
    if (canvas.width !== pixelWidth || canvas.height !== pixelHeight) {
        canvas.width = pixelWidth;
        canvas.height = pixelHeight;
    }
    const ctx = canvas.getContext('2d');
    const width = canvas.width / dpr;
    const height = canvas.height / dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, width, height);
    if (!index.frame) return;

    const mapper = createCoordinateMapper({canvasWidth: width, canvasHeight: height,
        videoWidth: video.videoWidth, videoHeight: video.videoHeight, perception: index.frame});
    const target = {ctx, canvas, video, mapper, layout: {left: 0, right: 0, performanceY: 10}, now};
    const ordered = [...overlays].sort((a, b) => (featureFor(a.featureId)?.renderOrder ?? 0)
        - (featureFor(b.featureId)?.renderOrder ?? 0));
    for (const overlay of ordered) {
        if (!overlay.enabled) continue;
        const resolved = resolveOverlay(index, overlay);
        if (['waiting', 'unsupported'].includes(resolved.state) || resolved.settings.opacity <= 0) continue;
        ctx.save();
        try {
            ctx.globalAlpha = resolved.settings.opacity ?? 1;
            resolved.feature.render({...target, overlay}, resolved, resolved.settings);
        } finally {
            ctx.restore();
        }
    }
}
