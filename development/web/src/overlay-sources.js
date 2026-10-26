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

const SOURCE_FIELDS = [
    'payloadType', 'contentType', 'engine', 'model', 'inferElementId', 'labelFamily',
    'producerInstanceId', 'producerComponent', 'producerImplementation',
];
const SOURCE_FIELD_LABELS = {
    payloadType: 'Payload type', contentType: 'Content type', engine: 'Engine', model: 'Model',
    inferElementId: 'Inference element', labelFamily: 'Label family',
    producerInstanceId: 'Producer instance', producerComponent: 'Component', producerImplementation: 'Implementation',
};

export function outputSource(layer) {
    return {
        payloadType: layer.payloadType || '',
        contentType: layer.contentType || '',
        engine: layer.engine || '',
        model: layer.model || '',
        inferElementId: layer.inferElementId || '',
        labelFamily: layer.labelFamily || '',
        producerInstanceId: layer.producer?.instanceId || '',
        producerComponent: layer.producer?.component || '',
        producerImplementation: layer.producer?.implementation || '',
    };
}

export function sourceKey(source) {
    return JSON.stringify(SOURCE_FIELDS.map((field) => source[field]));
}

export function validSource(source) {
    return SOURCE_FIELDS.every((field) => typeof source?.[field] === 'string') && source.payloadType.length > 0;
}

// Relationship identity is deliberately independent of model, engine, label
// family and the full selector used to persist a user's source choice.
export function producerKey(source) {
    return source.producerInstanceId
        ? JSON.stringify([source.producerComponent, source.producerInstanceId]) : null;
}

export function objectId(value) {
    return value == null || String(value) === '0' ? '' : String(value);
}

export function trackKey(source, id) {
    const producer = producerKey(source);
    return producer && objectId(id) ? JSON.stringify([producer, objectId(id)]) : null;
}

export function sourceLabel(source) {
    if (source.payloadType === 'PerformanceOverlay') return 'Pipeline performance';
    return source.model || source.producerImplementation || source.producerInstanceId
        || source.inferElementId || source.producerComponent || 'Unspecified source';
}

export function sourceDetails(source) {
    return SOURCE_FIELDS.map((field) => [SOURCE_FIELD_LABELS[field], source[field] || 'Not provided']);
}

// Labels are presentation only. Group by the unchanged full source key first.
export function sourceLabels(sources, groupByPayload = true) {
    const groups = new Map();
    for (const source of new Map([...sources].map((source) => [sourceKey(source), source])).values()) {
        const groupKey = JSON.stringify([groupByPayload ? source.payloadType : '', sourceLabel(source)]);
        if (!groups.has(groupKey)) groups.set(groupKey, []);
        groups.get(groupKey).push(source);
    }
    const labels = new Map();
    for (const group of groups.values()) {
        if (group.length === 1) {
            labels.set(sourceKey(group[0]), sourceLabel(group[0]));
            continue;
        }
        const fields = SOURCE_FIELDS.filter((field) => new Set(group.map((source) => source[field])).size > 1);
        const qualify = (source, selected) => selected.map((field) =>
            source[field] || `${SOURCE_FIELD_LABELS[field]} not provided`).join(' · ');
        // The identity has a fixed, small field set. Try the fewest qualifiers,
        // then the shortest text, so a short engine name beats a long instance ID.
        const choices = [[]];
        for (const field of fields) choices.push(...choices.map((choice) => [...choice, field]));
        const length = (choice) => group.reduce((total, source) => total + qualify(source, choice).length, 0);
        choices.sort((a, b) => a.length - b.length || length(a) - length(b));
        const choice = choices.find((candidate) => new Set(group.map((source) => qualify(source, candidate))).size === group.length);
        for (const source of group) {
            // Labelled JSON is a last resort for metadata containing ambiguous delimiters.
            const qualifier = choice ? qualify(source, choice)
                : JSON.stringify(Object.fromEntries(fields.map((field) => [SOURCE_FIELD_LABELS[field], source[field]])));
            labels.set(sourceKey(source), `${sourceLabel(source)} (${qualifier})`);
        }
    }
    return labels;
}

// Identical provenance denotes one output, including repeated payloads from crop loops.
// Never use array positions or per-frame object IDs as persistent source identities.
export function collectOutputs(frame) {
    const outputs = new Map();
    for (const layer of frame?.layers || []) {
        if (!layer.payloadType || layer.payloadType === 'FrameContext') continue;
        const source = outputSource(layer);
        const key = sourceKey(source);
        if (!outputs.has(key)) outputs.set(key, {key, source, layers: []});
        outputs.get(key).layers.push(layer);
    }
    return outputs;
}

function append(index, key, value) {
    if (!key) return;
    if (!index.has(key)) index.set(key, []);
    index.get(key).push(value);
}

// Build once when a metadata frame arrives. Candidate arrays preserve ambiguity;
// plugins must not pick an arbitrary first object or trajectory.
export function indexFrame(frame) {
    const outputs = collectOutputs(frame);
    const index = {frame, outputs, byPayload: new Map(), byProducer: new Map(), objects: new Map(), byTrack: new Map()};
    for (const output of outputs.values()) {
        append(index.byPayload, output.source.payloadType, output);
        append(index.byProducer, producerKey(output.source), output);
        output.items = [];
        for (const layer of output.layers) {
            for (const item of layer.detections || []) {
                if (!item?.data) continue;
                const entry = {...item, layer, source: output.source};
                output.items.push(entry);
                append(index.objects, objectId(item.data.uuid), entry);
                append(index.byTrack, trackKey(output.source, item.data.trackId), entry);
            }
        }
    }
    return index;
}
