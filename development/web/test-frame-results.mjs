/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

import {execFileSync} from 'node:child_process';
import {createRequire} from 'node:module';
import {mkdirSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';

const root = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(root, '..', '..');
const modulesRoot = process.env.OPK_WEB_NODE_MODULES
    || process.env.NODE_PATH
    || '/usr/local/lib/node_modules';
const require = createRequire(import.meta.url);

function resolvePackage(name, fallback) {
    try {
        return require.resolve(name);
    } catch {
        return path.join(modulesRoot, fallback);
    }
}

const esbuildPath = resolvePackage('esbuild-wasm', 'esbuild-wasm/lib/main.js');
const flatbuffersPath = resolvePackage('flatbuffers', 'flatbuffers/mjs/flatbuffers.js');
const {build} = await import(pathToFileURL(esbuildPath));
const outputDirectory = process.env.OPK_WEB_TEST_OUTPUT_DIR || tmpdir();
const output = path.resolve(outputDirectory, `opk-frame-results-test-${process.pid}.mjs`);

mkdirSync(outputDirectory, {recursive: true});
try {
    await build({
        absWorkingDir: repoRoot,
        alias: {flatbuffers: flatbuffersPath},
        bundle: true,
        entryPoints: [path.join(root, 'tests', 'frame-results.test.mjs')],
        format: 'esm',
        outfile: output,
        platform: 'node',
        sourcemap: 'inline',
        target: ['node20'],
    });

    execFileSync(process.execPath, ['--test', ...process.argv.slice(2), output], {stdio: 'inherit'});
} finally {
    rmSync(output, {force: true});
}
