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

import {BitmapEncoding} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/bitmap-encoding.js';
import {OpacityMeaning} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/opacity-meaning.js';
import {validImageSpace, imageBox} from './image-geometry.js';

export {BitmapEncoding, OpacityMeaning};

export function validBitmap(data) {
    const bytes = data?.encoding === BitmapEncoding.Uint8 ? 1 : data?.encoding === BitmapEncoding.Uint32LE ? 4 : 0;
    return bytes > 0 && validImageSpace(data.space)
        && Number.isInteger(data.width) && data.width > 0 && data.width <= 0xffffffff
        && Number.isInteger(data.height) && data.height > 0 && data.height <= 0xffffffff
        && Number.isSafeInteger(data.width * data.height * bytes)
        && data.pixels instanceof Uint8Array && data.pixels.length === data.width * data.height * bytes;
}

export function validMask(data, type) {
    if (!validBitmap(data)) return false;
    if (type === 'SegmentationMask') return data.encoding === BitmapEncoding.Uint8
        && [OpacityMeaning.BackgroundOpacity, OpacityMeaning.RegionOpacity].includes(data.meaning);
    const identified = typeof data.labelSet === 'string' && data.labelSet.length > 0;
    if (type === 'SemanticSegmentationMask') return identified && (data.voidValue == null
        || Number.isInteger(data.voidValue) && data.voidValue >= 0
        && data.voidValue <= (data.encoding === BitmapEncoding.Uint8 ? 255 : 0xffffffff));
    if (type !== 'InstanceSegmentationMask' || data.encoding !== BitmapEncoding.Uint8) return false;
    if (data.classId != null && (!identified || !Number.isInteger(data.classId) || data.classId < 0 || data.classId > 0xffffffff)) return false;
    if (data.confidence != null && (!Number.isFinite(data.confidence) || data.confidence < 0 || data.confidence > 1)) return false;
    if (data.box && (!imageBox(data.box) || String(data.box.space.frameId) !== String(data.space.frameId)
        || data.box.space.width !== data.space.width || data.box.space.height !== data.space.height)) return false;
    return data.pixels.every(value => value <= 1);
}

export function maskData(item, type) {
    const bitmap = item?.bitmap;
    const data = {width: bitmap?.width, height: bitmap?.height, encoding: bitmap?.encoding,
        pixels: bitmap?.pixels != null ? Uint8Array.from(bitmap.pixels) : undefined, space: bitmap?.space,
        meaning: item?.meaning, labelSet: item?.labelSet, voidValue: item?.voidValue,
        classId: item?.classId, confidence: item?.confidence, box: item?.box};
    return {...data, valid: validMask(data, type)};
}
