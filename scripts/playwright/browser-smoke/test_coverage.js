const assert = require('node:assert/strict');
const { mkdtemp, readFile, rm, writeFile } = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const test = require('node:test');

const { generateLcov } = require('./v8-coverage-to-lcov');

test('converts inline source-mapped Chromium coverage to LCOV', async () => {
  const directory = await mkdtemp(path.join(os.tmpdir(), 'pek-playwright-coverage-'));
  try {
    const original = 'const answer = 42;\nconsole.log(answer);\n';
    const sourceMap = Buffer.from(JSON.stringify({
      version: 3,
      file: 'pek-web.js',
      sources: ['../src/example.js'],
      sourcesContent: [original],
      names: [],
      mappings: 'AAAA;AACA',
    })).toString('base64');
    const generated = `${original}//# sourceMappingURL=data:application/json;base64,${sourceMap}\n`;
    await writeFile(path.join(directory, 'coverage.json'), JSON.stringify([{
      url: 'http://127.0.0.1:9999/pek-web.js',
      source: generated,
      functions: [{
        functionName: '',
        isBlockCoverage: true,
        ranges: [{ startOffset: 0, endOffset: generated.length, count: 1 }],
      }],
    }]));

    const output = path.join(directory, 'lcov.info');
    await generateLcov(directory, output);
    const lcov = await readFile(output, 'utf8');
    assert.match(lcov, /SF:development\/web\/src\/example\.js/);
    assert.match(lcov, /DA:1,1/);
    assert.match(lcov, /LH:[1-9]/);
  } finally {
    await rm(directory, { force: true, recursive: true });
  }
});
