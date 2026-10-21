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

import assert from 'node:assert/strict';
import {access, readFile} from 'node:fs/promises';
import test from 'node:test';

test('WebUI uses published Wafer tokens and complete offline design assets', async () => {
    const content = new URL('../content/', import.meta.url);
    const css = await readFile(new URL('style.css', content), 'utf8');
    const html = await readFile(new URL('index.html', content), 'utf8');
    const wafer = await readFile(new URL('vendor/wafer/wafer.css', content), 'utf8');
    const provided = new Set([...wafer.matchAll(/(--wfr-[\w-]+)\s*:/g)].map(match => match[1]));
    for (const [, token] of css.matchAll(/var\((--wfr-[\w-]+)/g)) {
        assert.ok(provided.has(token), `Unknown Wafer token: ${token}`);
    }
    assert.doesNotMatch(css, /--wfr-[\w-]+\s*:/, 'App CSS must not redefine Wafer tokens');
    assert.doesNotMatch(html, /fontawesome|fa-solid/);
    assert.equal([...html.matchAll(/href="\/vendor\/wafer\/wafer\.css"/g)].length, 1);
    for (const asset of ['vendor/wafer/wafer.css', 'vendor/lucide/icons.css']) {
        const url = new URL(asset, content);
        const stylesheet = await readFile(url, 'utf8');
        for (const [, relative] of stylesheet.matchAll(/url\(["']?([^"')]+)["']?\)/g)) {
            assert.ok(!relative.includes(':'), `Design asset must be local: ${relative}`);
            await access(new URL(relative, url));
        }
    }
});
