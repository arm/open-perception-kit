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

export function validLocalImage(value) {
    return typeof value === 'string' && (value === '' || (value.length <= 2_000_000
        && /^data:image\/(png|jpeg|webp);base64,[A-Za-z0-9+/]+={0,2}$/.test(value)));
}

// Store a bounded raster in the overlay settings, never a filesystem or remote URL.
export async function loadLocalImage(file) {
    if (!['image/png', 'image/jpeg', 'image/webp'].includes(file.type) || file.size > 10 * 1024 * 1024) {
        throw new Error('Choose a PNG, JPEG or WebP image up to 10 MiB.');
    }
    let image;
    try {
        image = await createImageBitmap(file);
        if (!image.width || !image.height || image.width * image.height > 40_000_000) {
            throw new Error('Invalid image dimensions');
        }
        const scale = Math.min(1, 1920 / Math.max(image.width, image.height));
        const canvas = document.createElement('canvas');
        canvas.width = Math.max(1, Math.round(image.width * scale));
        canvas.height = Math.max(1, Math.round(image.height * scale));
        canvas.getContext('2d').drawImage(image, 0, 0, canvas.width, canvas.height);
        const data = canvas.toDataURL('image/webp', 0.85);
        if (!validLocalImage(data)) throw new Error('Image is too large to save');
        return data;
    } catch {
        throw new Error('Could not prepare this image. Choose a smaller, valid raster image.');
    } finally {
        image?.close();
    }
}
