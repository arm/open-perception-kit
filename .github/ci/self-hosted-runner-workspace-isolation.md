# Self-Hosted Runner Workspace Isolation

## Summary

Some CI jobs in this repository run Docker Compose services with the checked out
repository bind-mounted into the container as `/work`. The setup path then
recursively changes ownership of `/work`.

On self-hosted runners this is dangerous because the checkout lives in a
persistent host workspace. Once a container rewrites ownership of the mounted
tree, later jobs can fail before the real checks even start.

## Observed symptoms

Typical failures appear during `actions/checkout`, for example:

- `fatal: Unable to create '.git/index.lock': Permission denied`
- checkout cleanup failing to unlink tracked files such as `.clang-format`

The failure is cross-job and cross-workflow. One job can leave the workspace in
a state that breaks later, unrelated jobs.

## Root cause

The issue comes from the combination of:

1. a persistent self-hosted runner workspace
2. bind-mounting the host checkout into the container as `/work`
3. recursively running `chown -R` on `/work`
4. sharing fixed Compose container names across jobs

The critical ownership-changing line currently lives in
`.devcontainer/setup.sh`.

## Containment used in this repository

This repository contains the blast radius rather than removing the `chown`
behaviour entirely:

- each Docker Compose based CI job gets a unique checkout path
- each such job gets a unique `COMPOSE_PROJECT_NAME`
- fixed `container_name` entries are avoided in the CI compose file
- the self-hosted Sonar workflows tear down Compose resources and delete their
  isolated checkout directory in an `if: always()` cleanup step

This ensures that one job does not reuse another job's poisoned checkout path or
Docker resource names.

## Why the warning stays near `chown`

The `chown -R /work` line is easy to cargo-cult into new CI paths because it
looks harmless inside a dev container. It is not harmless on a self-hosted
runner with bind-mounted source code.

That line should not be copied into new CI flows without understanding this
document first.

## Remaining risk

This is still containment, not the final cleanup:

- the CI setup path still mutates the mounted checkout
- cleanup steps can still fail partially if the runner host or Docker daemon is
  already in a broken state

The long-term fix is a CI-specific setup flow that does not rewrite the mounted
repository at all.
