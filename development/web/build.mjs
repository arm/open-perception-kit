import {cp, mkdir, readFile, rm} from 'node:fs/promises';
import {createRequire} from 'node:module';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';

const root = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(root, '..', '..');
const output = path.join(root, 'content', 'pek-web.js');
const dependencyStage = path.join(root, '.pek-web-build');
const command = process.argv[2] || 'generate';
const modulesRoot = process.env.PEK_WEB_NODE_MODULES
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
    ? path.join(tmpdir(), `pek-web-${process.pid}.js`)
    : output;

try {
    await rm(dependencyStage, {force: true, recursive: true});
    await mkdir(dependencyStage, {recursive: true});
    await cp(flatbuffersRoot, stagedFlatbuffersRoot, {recursive: true});

    await build({
        absWorkingDir: repoRoot,
        alias: {flatbuffers: flatbuffersPath},
        banner: {js: '// Copyright (C) 2025 Arm Limited. All rights reserved.'},
        bundle: true,
        entryPoints: [path.join(root, 'src', 'app.js')],
        format: 'esm',
        legalComments: 'none',
        minify: false,
        outfile: candidate,
        platform: 'browser',
        sourcemap: process.env.PEK_WEB_COVERAGE === '1' ? 'inline' : false,
        target: ['es2020'],
    });

    if (command === 'check') {
        const [expected, actual] = await Promise.all([
            readFile(output),
            readFile(candidate),
        ]);
        if (!expected.equals(actual)) {
            throw new Error('peksink WebUI bundle is stale; run ./scripts/peksink-web.sh generate');
        }
    }
} finally {
    await rm(dependencyStage, {force: true, recursive: true});
    if (command === 'check') {
        await rm(candidate, {force: true});
    }
}
