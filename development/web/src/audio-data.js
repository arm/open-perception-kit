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
export function validAudioResult(data, probability, decision) {
    const score = data[probability];
    if (typeof score !== 'number' || !Number.isFinite(score) || score < 0 || score > 1 || typeof data[decision] !== 'boolean') return false;
    const unsigned = value => value != null && (typeof value !== 'number' || Number.isSafeInteger(value))
        && /^[0-9]{1,20}$/.test(String(value)) && BigInt(value) <= 0xffffffffffffffffn;
    if (!unsigned(data.windowStartSample) || !unsigned(data.windowEndSample)
        || BigInt(data.windowEndSample) <= BigInt(data.windowStartSample)) return false;
    const start = data.windowStartTsNs, end = data.windowEndTsNs;
    return start == null && end == null || unsigned(start) && unsigned(end) && BigInt(start) < BigInt(end);
}
