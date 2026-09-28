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

import {cp, mkdir, readFile, rm} from 'node:fs/promises';
import {createRequire} from 'node:module';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';

const root = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(root, '..', '..');
const output = path.join(root, 'content', 'opk-web.js');
const dependencyStage = path.join(root, '.opk-web-build');
const command = process.argv[2] || 'generate';
const modulesRoot = process.env.OPK_WEB_NODE_MODULES
    || process.env.NODE_PATH
    || '/usr/local/lib/node_modules';

if (!['generate', 'check'].includes(command)) {
    throw new Error(`unknown build command: ${command}`);
}

const require = createRequire(import.meta.url);
const localEsbuild = (() => {
    try {
        return require.resolve('esbuild-wasm');
    } catch {
        return null;
    }
})();
const localFlatbuffers = (() => {
    try {
        return require.resolve('flatbuffers/mjs/flatbuffers.js');
    } catch {
        return null;
    }
})();
const esbuildPath = localEsbuild
    || path.join(modulesRoot, 'esbuild-wasm', 'lib', 'main.js');
const flatbuffersRoot = localFlatbuffers
    ? path.resolve(localFlatbuffers, '..', '..')
    : path.join(modulesRoot, 'flatbuffers');
const stagedFlatbuffersRoot = path.join(dependencyStage, 'flatbuffers');
const flatbuffersPath = path.join(stagedFlatbuffersRoot, 'mjs', 'flatbuffers.js');
const {build} = await import(pathToFileURL(esbuildPath));
const candidate = command === 'check'
    ? path.join(tmpdir(), `opk-web-${process.pid}.js`)
    : output;

try {
    await rm(dependencyStage, {force: true, recursive: true});
    await mkdir(dependencyStage, {recursive: true});
    await cp(flatbuffersRoot, stagedFlatbuffersRoot, {recursive: true});

    await build({
        absWorkingDir: repoRoot,
        alias: {flatbuffers: flatbuffersPath},
        banner: {js: `/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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
 */`},
        bundle: true,
        entryPoints: [path.join(root, 'src', 'app.js')],
        format: 'esm',
        legalComments: 'none',
        minify: false,
        outfile: candidate,
        platform: 'browser',
        sourcemap: process.env.OPK_WEB_COVERAGE === '1' ? 'inline' : false,
        target: ['es2020'],
    });

    if (command === 'check') {
        const [expected, actual] = await Promise.all([
            readFile(output),
            readFile(candidate),
        ]);
        if (!expected.equals(actual)) {
            throw new Error('opksink WebUI bundle is stale; run ./scripts/opksink-web.sh generate');
        }
    }
} finally {
    await rm(dependencyStage, {force: true, recursive: true});
    if (command === 'check') {
        await rm(candidate, {force: true});
    }
}
