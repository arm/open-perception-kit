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

import {ClassificationKind} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/classification-kind.js';

export const validProbability = value => typeof value === 'number' && Number.isFinite(value) && value >= 0 && value <= 1;

export function validCandidate(classification, candidate) {
    if (!candidate || (candidate.confidence != null && !validProbability(candidate.confidence))) return false;
    if (classification.kind === ClassificationKind.Categorical)
        return Boolean(classification.labelSet) && Number.isInteger(candidate.classId) && candidate.classId >= 0;
    if (classification.kind === ClassificationKind.RecognizedText)
        return !classification.labelSet && candidate.classId == null;
    return false;
}

export function presenceOutcome(data) {
    const result = data?.result;
    if (!result || typeof result.personPresent !== 'boolean') return null;
    if (result.yesScore == null && result.noScore == null) return result.personPresent;
    return Number.isFinite(result.yesScore) && Number.isFinite(result.noScore) ? result.personPresent : null;
}

export function contactOutcome(data) {
    const result = data?.result;
    if (!result || typeof result.inContact !== 'boolean'
        || (result.confidence != null && !validProbability(result.confidence))) return null;
    return result.inContact;
}
