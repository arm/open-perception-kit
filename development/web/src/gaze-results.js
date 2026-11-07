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

import {validProbability} from './classification-results.js';

export function validGaze(data) {
    return Boolean(data?.angles) && Number.isFinite(data.angles.yawDegrees)
        && Number.isFinite(data.angles.pitchDegrees)
        && (data.confidence == null || validProbability(data.confidence));
}

export function wrapDegrees(value) {
    const angle = value % 360;
    return angle > 180 ? angle - 360 : angle < -180 ? angle + 360 : angle;
}
