---
sidebar_position: 12
sidebar_label: Branching Policy
---

# Branching Policy

`main` is the default branch and integration branch. There is no active
`develop` branch. Normal development, fixes, and release preparation reach
`main` through pull requests.

## Branch names and purpose

Use a ticketed branch name for contributor work:

| Branch family | Purpose | Example |
| --- | --- | --- |
| `feature/EXPKITS-*` | Planned development | `feature/EXPKITS-1234/add-camera-contact-parser` |
| `bugfix/EXPKITS-*` | Normal fixes | `bugfix/EXPKITS-5678/fix-ui-timeout` |
| `hotfix/EXPKITS-*` | Urgent fixes to released behavior | `hotfix/EXPKITS-9012/fix-release-crash` |
| `release/EXPKITS-*` | Release preparation | `release/EXPKITS-1234-create-release-1.2.3` |

Branch from `main` and target `main` for each of these flows. Dependabot manages
its own `dependabot/*` branches. Ticketed naming is the contribution convention;
the repository does not currently have a global branch-name ruleset.

## Development flow

1. Create a ticketed branch from the current `main`.
2. Implement and validate the change using the repository's checks.
3. Open a pull request into `main` with a Conventional Commit title and the
   `Goal`, `Change`, and `Testing` sections from the PR template.
4. Rebase onto the target branch when an update is needed. Do not merge `main`
   into the feature branch.
5. Resolve review threads, obtain the required approvals, and pass the required
   checks before squash-merging.

Sign every commit cryptographically and include a DCO `Signed-off-by` trailer;
these are separate requirements. For example, use `git commit -S -s`,
`git rebase --gpg-sign --signoff`, and `git cherry-pick -S -s`. Verify signatures
and sign-offs again after rewriting commits.

Hotfixes follow the same PR flow into `main`. There is no back-merge to a second
integration branch. For a native PR stack, only the bottom PR targets `main`;
each upper PR targets the branch immediately below it. Rebase the stack as its
base changes and merge from bottom to top.

## Release flow

An ordinary PR into `main` does not require a new product version, and merging
it does not publish a product release.

When the integrated changes are ready to ship, prepare the version, changelog,
and generated version consumers on a ticketed `release/*` branch and merge
that PR into `main`. Then manually start the **Release** workflow from `main`.
It uses the version already committed in the selected source; it has no version
override or automatic prerelease version. Published versions must not be
overwritten.

See the repository's
[release process](https://github.com/arm/open-perception-kit/blob/main/docs/arch/release-process.md#release-packages)
for the exact inputs, artifacts, validation, and publication order.

## Enforcement

The active `main` ruleset targets the default branch. It requires:

- pull requests with one approval, code-owner review for owned paths, and all
  review threads resolved;
- dismissal of stale approvals after new commits, approval of the latest push
  by someone other than its author, and extra approval for unattributed changes;
- required status checks to pass with the branch up to date;
- signed commits and a DCO sign-off in commit messages;
- linear history and squash merges, with branch creation, deletion, and force
  pushes restricted on the protected ref.

Repository administrators have a configured bypass. GitHub settings are
maintained manually; inspect the
[current ruleset](https://github.com/arm/open-perception-kit/rules/23838289)
for the required check names and current settings.
