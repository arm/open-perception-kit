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
- CI workflow migration
- extra quality rules beyond the existing local pre-commit bundle

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

Light mapping:

- Dev Container pre-commit hook: this is the local truth for the pre-commit bundle.
- Host `./scripts/pre-commit/run.sh`: runs the same pre-commit-stage checks on the host through the dedicated container.
- Host `./scripts/pre-commit/run.sh commit-msg <path>`: mirrors the `commit-msg` hook path.
- CI PR quality: broader validation path, today driven through `expkits-ci --all-checks` on the PR diff.
- CI full quality: check-only CI run for the formatter/license/secrets bundle on the full tracked tree.

The wrapper builds a dedicated runtime image up front and then reuses it for
hook execution. There is no hidden image rebuild during a normal commit.

To keep local worktree checkouts working, the runtime mounts both the working
tree and the external git common directory when `.git` points outside the
checkout.
