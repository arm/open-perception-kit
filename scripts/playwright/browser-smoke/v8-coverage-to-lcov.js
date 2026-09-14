#!/usr/bin/env node
const { mkdir, readFile, readdir, writeFile } = require('node:fs/promises');
const path = require('node:path');

const v8ToIstanbul = require('v8-to-istanbul');

const SOURCE_ROOT = 'development/web/src/';

function sourcePath(filePath) {
  const normalized = filePath.replaceAll('\\', '/');
  const index = normalized.lastIndexOf(SOURCE_ROOT);
  return index === -1 ? null : normalized.slice(index);
}

function mergeCoverage(linesByFile, istanbulCoverage) {
  for (const coverage of Object.values(istanbulCoverage)) {
    const filePath = sourcePath(coverage.path);
    if (!filePath) {
      continue;
    }

    const lines = linesByFile.get(filePath) || new Map();
    for (const [id, location] of Object.entries(coverage.statementMap)) {
      const line = location.start.line;
      lines.set(line, Math.max(lines.get(line) || 0, coverage.s[id] || 0));
    }
    linesByFile.set(filePath, lines);
  }
}

function toLcov(linesByFile) {
  const output = [];
  for (const filePath of [...linesByFile.keys()].sort()) {
    const lines = linesByFile.get(filePath);
    const entries = [...lines.entries()].sort(([left], [right]) => left - right);
    output.push('TN:RPi5 Playwright', `SF:${filePath}`);
    for (const [line, count] of entries) {
      output.push(`DA:${line},${count}`);
    }
    output.push(
      `LF:${entries.length}`,
      `LH:${entries.filter(([, count]) => count > 0).length}`,
      'end_of_record',
    );
  }
  return `${output.join('\n')}\n`;
}

async function generateLcov(inputDir, outputPath) {
  const inputFiles = (await readdir(inputDir))
    .filter((file) => file.endsWith('.json'))
    .sort();
  if (inputFiles.length === 0) {
    throw new Error(`No Playwright coverage files found in ${inputDir}`);
  }

  const linesByFile = new Map();
  for (const inputFile of inputFiles) {
    const entries = JSON.parse(await readFile(path.join(inputDir, inputFile), 'utf8'));
    for (const entry of entries) {
      const converter = v8ToIstanbul('development/web/content/pek-web.js', 0, {
        source: entry.source,
      });
      await converter.load();
      converter.applyCoverage(entry.functions);
      mergeCoverage(linesByFile, converter.toIstanbul());
      converter.destroy();
    }
  }

  if (linesByFile.size === 0) {
    throw new Error('Playwright coverage did not map to development/web/src');
  }
  await mkdir(path.dirname(outputPath), { recursive: true });
  await writeFile(outputPath, toLcov(linesByFile));
}

if (require.main === module) {
  if (process.argv.length !== 4) {
    console.error('Usage: v8-coverage-to-lcov.js <input-dir> <output-file>');
    process.exit(2);
  }
  generateLcov(process.argv[2], process.argv[3]).catch((error) => {
    console.error(error.message);
    process.exit(1);
  });
}

module.exports = { generateLcov, sourcePath, toLcov };
