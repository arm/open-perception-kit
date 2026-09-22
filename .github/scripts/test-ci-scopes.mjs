import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { test } from 'node:test';

const filters = fileURLToPath(new URL('../filters.yml', import.meta.url));
const action = resolve(process.env.PATHS_FILTER_ACTION, 'dist/index.js');
const checks = ['meson', 'valgrind', 'sdk', 'clang_tidy', 'clang_tidy_all'];
const native = ['meson', 'valgrind', 'clang_tidy', 'clang_tidy_all'];
const source = 'development/elements/opkinfer/opkinfer.cpp';
const scenarios = [
  { name: 'Python build dependency update', files: ['requirements/build.txt'], selected: [] },
  { name: 'Python model dependency update', files: ['requirements/models.txt'], selected: [] },
  { name: 'documentation update', files: ['docs/public/index.md'], selected: [] },
  { name: 'Dependabot configuration update', files: ['.github/dependabot.yml'], selected: [] },
  { name: 'C++ source update', files: [source], selected: ['meson', 'valgrind', 'clang_tidy'], sources: [source] },
  { name: 'new C++ source', files: [source], added: [source], selected: ['meson', 'valgrind', 'clang_tidy'], sources: [source] },
  { name: 'C++ header update', files: ['development/common/example.hpp'], selected: native },
  { name: 'generated header template update', files: ['development/config-validator/EmbeddedSchemas.h.in'], selected: native },
  { name: 'Meson dependency update', files: ['requirements/meson.txt'], selected: native },
  { name: 'native toolchain update', files: ['requirements/build.json'], selected: checks },
  { name: 'schema update', files: ['schemas/perception/frame.fbs'], selected: checks },
  { name: 'SDK toolchain update', files: ['requirements/sdk.txt'], selected: checks },
  { name: 'SDK workflow update', files: ['.github/workflows/sdk-tests.yml'], selected: ['sdk'] },
  { name: 'Meson workflow update', files: ['.github/workflows/meson-tests.yml'], selected: ['meson'] },
  { name: 'Valgrind workflow update', files: ['.github/workflows/valgrind-tests.yml'], selected: ['valgrind'] },
  { name: 'clang-tidy workflow update', files: ['.github/workflows/clang-tidy.yml'], selected: ['clang_tidy', 'clang_tidy_all'] },
  { name: 'web application update', files: ['development/web/src/main.js'], selected: ['meson'] },
  { name: 'model configuration update', files: ['config/models/yolo26/model.json'], selected: ['meson', 'valgrind'] },
  { name: 'Python runtime lock update', files: ['development/ops-python/runtime.json'], selected: native },
  { name: 'path rules update', files: ['.github/filters.yml'], selected: checks },
  { name: 'change detection workflow update', files: ['.github/workflows/changes.yml'], selected: checks },
  { name: 'deleted C++ source', files: [], deleted: [source], selected: ['meson', 'valgrind'] },
  { name: 'renamed C++ source', files: ['development/elements/opkinfer/renamed.cpp'], added: ['development/elements/opkinfer/renamed.cpp'], deleted: [source], selected: ['meson', 'valgrind', 'clang_tidy'], sources: ['development/elements/opkinfer/renamed.cpp'] },
  { name: 'mixed dependency and C++ changes', files: ['requirements/build.txt', source], selected: ['meson', 'valgrind', 'clang_tidy'], sources: [source] },
];

for (const { mode, ...scenario } of ['local', 'push'].flatMap(mode => scenarios.map(scenario => ({ mode, ...scenario })))) {
  test(`${mode}: when ${scenario.name}, selects only the relevant checks and source files`, () => {
    const temporary = mkdtempSync(join(tmpdir(), 'opk-ci-scopes-'));
    const repository = join(temporary, 'repository');
    const output = join(temporary, 'outputs');
    const environment = { ...process.env, GIT_CONFIG_GLOBAL: join(temporary, 'gitconfig') };
    mkdirSync(repository);
    const git = (...args) => execFileSync('git', args, { cwd: repository, env: environment, stdio: 'pipe' });
    const write = (path, content) => {
      const destination = join(repository, path);
      mkdirSync(dirname(destination), { recursive: true });
      writeFileSync(destination, content);
    };
    try {
      git('init', '--initial-branch=main');
      git('config', 'user.name', 'CI scope test');
      git('config', 'user.email', 'ci@example.invalid');
      write('README.md', 'Baseline\n');
      for (const path of [...scenario.files, ...(scenario.deleted ?? [])]) {
        if (!scenario.added?.includes(path)) write(path, 'Original\n');
      }
      git('add', '.');
      git('-c', 'core.hooksPath=/dev/null', 'commit', '--no-verify', '-m', 'Baseline');
      const before = git('rev-parse', 'HEAD').toString().trim();
      for (const path of scenario.files) write(path, 'Changed\n');
      for (const path of scenario.deleted ?? []) rmSync(join(repository, path));
      git('add', '--all');
      writeFileSync(output, '');
      const event = join(temporary, 'event.json');
      writeFileSync(event, JSON.stringify({ before, repository: { default_branch: 'main' } }));
      if (mode === 'push') git('-c', 'core.hooksPath=/dev/null', 'commit', '--no-verify', '-m', 'Change');

      execFileSync(process.execPath, [action], {
        cwd: repository,
        env: {
          ...environment,
          INPUT_BASE: mode === 'push' ? 'refs/heads/main' : 'HEAD',
          INPUT_REF: '',
          INPUT_TOKEN: '',
          INPUT_FILTERS: filters,
          'INPUT_LIST-FILES': 'json',
          'INPUT_WORKING-DIRECTORY': '',
          GITHUB_OUTPUT: output,
          GITHUB_WORKSPACE: repository,
          GITHUB_EVENT_NAME: mode === 'push' ? 'push' : 'workflow_dispatch',
          GITHUB_EVENT_PATH: event,
          GITHUB_REF: 'refs/heads/main',
        },
        stdio: 'pipe',
      });

      const outputs = Object.fromEntries([...readFileSync(output, 'utf8').matchAll(/^(\w+)<<(\S+)\n([\s\S]*?)\n\2$/gm)]
        .map(([, name, , value]) => [name, value]));
      assert.deepEqual(checks.map(name => [name, outputs[name]]), checks.map(name => [name, String(scenario.selected.includes(name))]));
      assert.deepEqual(JSON.parse(outputs.sources_files), scenario.sources ?? []);
    } finally {
      rmSync(temporary, { recursive: true, force: true });
    }
  });
}
