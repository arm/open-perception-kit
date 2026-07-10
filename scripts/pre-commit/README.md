# pre-commit scripts

This folder owns the host-side pre-commit container flow for `amp-dev-forge`.

## Scope

This folder intentionally keeps the rollout narrow:

- host shell setup and hook installation
- dedicated Docker runtime for the local quality checks
- portable git hook wrappers
- host-side smoke coverage

Out of scope here:

- Dev Container hook adoption
- CI workflow ownership

## Entry Points

Run the one-time host setup:

```bash
./scripts/pre-commit/setup.sh
```

After that, normal host-side commits use the installed hooks:

```bash
git commit
```

Manual entrypoints:

- `./scripts/pre-commit/run.sh`
- `./scripts/pre-commit/run.sh full`
- `./scripts/pre-commit/testing/host-smoke.sh`

## Behavior

The host-only wrapper keeps the existing local hook intent:

- `branch-naming`
- `commit-msg`
- `clang-format`
- `python-format`
- `cmake-format`
- `shell-format`
- `license-header`
- `check-secrets`
- `actionlint` for GitHub Actions workflow files only

Light mapping:

- Dev Container pre-commit hook: runs `expkits-ci --pre-commit-fix` plus the commit metadata hooks.
- Host `./scripts/pre-commit/run.sh`: runs the same `--pre-commit-fix` bundle on the host through the dedicated container.
- Host `./scripts/pre-commit/run.sh commit-msg <path>`: mirrors the `commit-msg` hook path.
- CI PR quality: runs `expkits-ci --ci-pr-checks`.
- CI full quality: runs `expkits-ci --ci-full-checks`.

The wrapper builds a dedicated runtime image up front and then reuses it for
hook execution. There is no hidden image rebuild during a normal commit.

If you keep multiple local clones with the same checkout directory name, set
`REPO_CHECKS_IMAGE_NAME=<unique-tag>` for both `setup.sh` and `run.sh` to
avoid local Docker image tag collisions between checkouts.

To keep local worktree checkouts working, the runtime mounts both the working
tree and the external git common directory when `.git` points outside the
checkout.
