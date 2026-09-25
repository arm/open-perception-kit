import {execFileSync} from 'node:child_process';
import {createRequire} from 'node:module';
import {mkdirSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

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
const esbuildBin = path.join(path.dirname(path.dirname(esbuildPath)), 'bin', 'esbuild');
const outputDirectory = process.env.OPK_WEB_TEST_OUTPUT_DIR || tmpdir();
const output = path.resolve(outputDirectory, `opk-frame-results-test-${process.pid}.mjs`);

mkdirSync(outputDirectory, {recursive: true});
try {
    execFileSync(process.execPath, [
        esbuildBin,
        path.join(root, 'tests', 'frame-results.test.mjs'),
        '--bundle',
        '--format=esm',
        '--platform=node',
        '--sourcemap=inline',
        '--target=node20',
        `--alias:flatbuffers=${flatbuffersPath}`,
        `--outfile=${output}`,
    ], {cwd: repoRoot, stdio: 'inherit'});

    execFileSync(process.execPath, ['--test', ...process.argv.slice(2), output], {stdio: 'inherit'});
} finally {
    rmSync(output, {force: true});
}
