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
import {TrackObservationState as State} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/track-observation-state.js';
import {TrackingTimeSource as Clock} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/tracking-time-source.js';
import {TrackAssociationKind as Kind} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/track-association-kind.js';
import {publishedPrecision} from './performance-data.js';
import {imageBox, validImageSpace} from './image-geometry.js';
export {State, Clock, Kind};

const positiveId = value => value != null && (typeof value !== 'number' || Number.isSafeInteger(value))
    && /^[1-9][0-9]{0,19}$/.test(String(value)) && BigInt(value) <= 0xffffffffffffffffn;
export function trackKey(identity) {
    return typeof identity?.trackerId === 'string' && identity.trackerId.length
        && typeof identity.sessionId === 'string' && identity.sessionId.length && positiveId(identity.trackId)
        ? JSON.stringify([identity.trackerId, identity.sessionId, String(identity.trackId)]) : null;
}
export function validSample(sample) {
    return positiveId(sample?.frameIndex) && Number.isFinite(sample.elapsedMs) && sample.elapsedMs >= 0
        && [State.Observed, State.Predicted].includes(sample.state)
        && [Clock.MediaDelta, Clock.FallbackDelta].includes(sample.timeSource);
}
export function sameSample(a, b) {
    return validSample(a) && validSample(b) && String(a.frameIndex) === String(b.frameIndex)
        && a.state === b.state && a.timeSource === b.timeSource && Math.abs(a.elapsedMs - b.elapsedMs) <= 1e-6;
}
export function traceMatchesCurrentTrack(trace, tracks) {
    return tracks.length === 0 || tracks.length === 1 && tracks[0].data.trackingValid === true
        && sameSample(trace.points?.at(-1)?.sample, tracks[0].data.sample);
}
export function validAssociation(value) {
    if (!value) return true;
    if ([Kind.Unspecified, Kind.New].includes(value.kind)) return value.score == null;
    if (value.score == null || !Number.isFinite(value.score)) return false;
    return value.kind === Kind.IoUScore ? value.score >= 0 && value.score <= 1
        : [Kind.CosineScore, Kind.Reacquired].includes(value.kind) && value.score >= -1 && value.score <= 1;
}
export function validTrack(track) {
    if (!trackKey(track.identity) || !validSample(track.sample) || !imageBox(track.box) || !validAssociation(track.association)) return false;
    if (track.sourceConfidence != null && (!Number.isFinite(track.sourceConfidence) || track.sourceConfidence < 0 || track.sourceConfidence > 1)) return false;
    if (track.sample.state === State.Predicted && track.association) return false;
    if ((track.sourceId == null) !== (track.sourceFrameIndex == null)) return false;
    return track.sourceId == null || positiveId(track.sourceId) && positiveId(track.sourceFrameIndex)
        && (track.sample.state === State.Observed ? BigInt(track.sourceFrameIndex) === BigInt(track.sample.frameIndex)
            : BigInt(track.sourceFrameIndex) < BigInt(track.sample.frameIndex));
}
export function validTrace(trace) {
    if (!trackKey(trace.identity) || !validImageSpace(trace.space)) return false;
    let previous = null;
    for (const point of trace.points || []) {
        if (!point || !Number.isFinite(point.x) || !Number.isFinite(point.y) || !validSample(point.sample)) return false;
        if (previous && (BigInt(point.sample.frameIndex) <= BigInt(previous.frameIndex) || point.sample.elapsedMs <= previous.elapsedMs)) return false;
        previous = point.sample;
    }
    return true;
}
export function trackLabel(track, displayIdentity) {
    const text = track.text || '';
    if (!displayIdentity || !trackKey(track.identity)) return text;
    let diagnostic = 'IOU:N/A';
    if (track.sample?.state === State.Predicted) diagnostic = 'PRED';
    else if (validAssociation(track.association)) {
        const association = track.association;
        if (association?.kind === Kind.New) diagnostic = 'NEW';
        else if (association?.score != null) {
            const prefix = association.kind === Kind.IoUScore ? 'IOU' : association.kind === Kind.Reacquired ? 'REID-R' : 'REID';
            const sign = association.score < 0 || Object.is(association.score, -0) ? '-' : '';
            diagnostic = `${prefix}:${sign}${publishedPrecision(Math.abs(association.score), 2).toFixed(2)}`;
        }
    }
    const suffix = `ID:${track.identity.trackId} ${diagnostic}`;
    return text ? `${text} [${suffix}]` : suffix;
}
export const identityData = value => value ? {trackerId: value.trackerId, sessionId: value.sessionId, trackId: String(value.trackId)} : null;
export const sampleData = value => value ? {...value, frameIndex: String(value.frameIndex)} : null;
