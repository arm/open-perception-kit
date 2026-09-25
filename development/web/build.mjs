import {spawn} from 'node:child_process';
import {cp, mkdir, readFile, rm} from 'node:fs/promises';
import {createRequire} from 'node:module';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

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
const esbuildBin = path.join(path.dirname(path.dirname(esbuildPath)), 'bin', 'esbuild');
const candidate = command === 'check'
    ? path.join(tmpdir(), `opk-web-${process.pid}.js`)
    : output;

try {
    await rm(dependencyStage, {force: true, recursive: true});
    await mkdir(dependencyStage, {recursive: true});
    await cp(flatbuffersRoot, stagedFlatbuffersRoot, {recursive: true});

    const args = [
        esbuildBin,
        path.join(root, 'src', 'app.js'),
        '--bundle',
        '--format=esm',
        '--legal-comments=none',
        '--platform=browser',
        '--target=es2020',
        `--alias:flatbuffers=${flatbuffersPath}`,
        '--banner:js=// Copyright (C) 2025 Arm Limited. All rights reserved.',
        `--outfile=${candidate}`,
    ];
    if (process.env.OPK_WEB_COVERAGE === '1') {
        args.push('--sourcemap=inline');
    }
    await new Promise((resolve, reject) => {
        const child = spawn(process.execPath, args, {cwd: repoRoot, stdio: 'inherit'});
        child.once('error', reject);
        child.once('exit', (code) => code === 0
            ? resolve()
            : reject(new Error(`esbuild exited with status ${code}`)));
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
