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

import {CoordinateUnits} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/coordinate-units.js';

export function validImageSpace(space) {
    const id = space?.frameId;
    return (typeof id === 'bigint' ? id > 0n : Number.isSafeInteger(id) && id > 0)
        && Number.isInteger(space.width) && space.width > 0
        && Number.isInteger(space.height) && space.height > 0
        && [space.originX, space.originY, space.extentWidth, space.extentHeight].every(Number.isFinite)
        && space.originX >= 0 && space.originY >= 0 && space.extentWidth > 0 && space.extentHeight > 0
        && space.originX + space.extentWidth <= space.width
        && space.originY + space.extentHeight <= space.height;
}

export function imagePoint(x, y, space) {
    return {x: space.originX + x, y: space.originY + y};
}

export function imageBox(box) {
    const space = box?.space;
    if (!validImageSpace(space) || ![box.x, box.y, box.width, box.height].every(Number.isFinite)
        || box.width <= 0 || box.height <= 0
        || ![CoordinateUnits.Pixels, CoordinateUnits.Normalized].includes(box.units)) return null;
    const unitX = box.units === CoordinateUnits.Normalized ? space.extentWidth : 1;
    const unitY = box.units === CoordinateUnits.Normalized ? space.extentHeight : 1;
    return {...imagePoint(box.x * unitX, box.y * unitY, space),
        width: box.width * unitX, height: box.height * unitY,
        frameWidth: space.width, frameHeight: space.height};
}
