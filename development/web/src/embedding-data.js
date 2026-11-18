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

import {EmbeddingMetric} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/embedding-metric.js';

// Vector usability is separate from resolving its subject observation.
export function usableEmbedding({space, values}) {
    return typeof space?.id === 'string' && space.id.length > 0
        && space.metric === EmbeddingMetric.Cosine && Array.isArray(values) && space.dimensions === values.length && values.length > 0
        && values.every(Number.isFinite) && values.some(value => value !== 0);
}
