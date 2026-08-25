import {execFileSync} from 'node:child_process';
import {createRequire} from 'node:module';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';

const root = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(root, '..', '..');
const modulesRoot = process.env.PEK_WEB_NODE_MODULES
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
const output = path.join(tmpdir(), `pek-frame-results-test-${process.pid}.mjs`);

await build({
    absWorkingDir: repoRoot,
    alias: {flatbuffers: flatbuffersPath},
    bundle: true,
    entryPoints: [path.join(root, 'tests', 'frame-results.test.mjs')],
    format: 'esm',
    outfile: output,
    platform: 'node',
    target: ['node20'],
});

execFileSync(process.execPath, ['--test', output], {stdio: 'inherit'});
