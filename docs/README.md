<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
SPDX-License-Identifier: Apache-2.0

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->

# Open Perception Kit Docs

The public Arm docs site source lives in `docs/public`. The docs config is `docs/public/docs-config.json`, Markdown image assets live under `docs/public/assets`, and site-root static assets live under `docs/public/static`.

Developer-facing architecture documentation lives in `docs/arch`.

PlantUML sources live in `docs/plantuml`. Generated PNGs are written to `docs/public/static/img` and referenced with paths relative to each Markdown file.

## Run Locally

First, make sure you can authenticate with your container engine by following [this guide](https://qa.developer.arm.com/dev-docs/arm-docs-github-action/getting-started/docker-auth/).

Make sure you are logged in and authenticated with Docker or Podman:

    echo "YOUR_GITHUB_TOKEN" | docker login ghcr.io -u "YOUR_GITHUB_USERNAME" --password-stdin
    # or
    echo "YOUR_GITHUB_TOKEN" | podman login ghcr.io -u "YOUR_GITHUB_USERNAME" --password-stdin

From the repository root, run:

```bash
./scripts/serve-docs.sh
```

The documentation scripts use `OPK_PROJECT_ROOT` when it is set and otherwise
derive the checkout from their own location. Set it to an absolute path when
invoking the scripts for a different checkout.

The script mounts `docs/public`, including its static assets, in the Arm docs preview image. It preserves the existing local script URL, `http://localhost:3003`.

If you need to run the container manually, the equivalent command is:

    docker run --rm -it --pull always -p 3003:3000 \
      -v "$(pwd)/docs/public":/workspace/docs-site:ro \
      ghcr.io/arm-debug/arm-docs-github-action/local:latest

Then open:

    http://localhost:3003

Stop the preview server with `Ctrl+C`.

## Publish From GitHub Actions

The publishing workflows are adapted from
[Arm-Debug/amp-dev-forge](https://github.com/Arm-Debug/amp-dev-forge/blob/6153af0407c823ea3d0eb9b54b89cce2b3422fc0/.github/workflows/docs-publish.yml).

| Workflow | Automatic trigger | Destination |
| --- | --- | --- |
| [Publish Docs (Staging)](../.github/workflows/docs-publish.yml) | Every push to `main` | [QA site](https://qa.developer.arm.com/dev-docs/open-perception-kit/) |
| [Publish Docs (Production)](../.github/workflows/docs-publish-production.yml) | Published stable GitHub Releases and the release CI call | [Public site](https://developer.arm.com/dev-docs/open-perception-kit/) |

Staging can also run manually from `main`, with an optional `publish_ref`.
Production's manual trigger requires an explicit commit SHA or ref in
`publish_ref` and makes that documentation publicly available without requiring
a product release. Manual runs must select `main` as the workflow branch. Before
running any documentation scripts, production verifies that the checked-out
commit is in `main`'s history; unmerged commits are rejected, while older commits
already on `main` remain eligible. This ancestry check does not verify that a
human reviewed the staging site. Merging workflow
changes into `main` does not publish to production. Draft releases and prerelease
events do not publish to production.

The [Release workflow](../.github/workflows/release.yml) calls the production
workflow after GitHub release publication succeeds, passing the exact release
commit. This covers automated releases, whose `GITHUB_TOKEN` events do not
trigger another workflow. A reusable caller must grant `contents: read` and
`id-token: write`. Direct release-event runs use the release's tagged commit.

Both workflows inject the checked-out commit into the existing build-info markers
in `docs/public/index.md` and validate the site before publishing. They use
[`ARM-software/docs-action` v0.5.0](https://github.com/ARM-software/docs-action/tree/3c5412fd76d4b2d67fd42d69d350abf29a6241c2).
The publish action's `staging` and `production` presets own each destination,
CloudFront invalidation, and analytics settings. The action does not expose a
site URL output, so each workflow summary uses its destination URL directly.

Both workflows pin their actions to full commit SHAs. Version v0.5.0 still contains
a nested tagged `actions/setup-node` reference, which conflicts with this
repository's SHA-pinning policy. Its `pnpm/action-setup` dependency is also blocked
by the repository's action allowlist. These issues must be resolved before either
workflow can publish.

### Publish a Docs Fix Without a Product Release

1. Merge the documentation fix or publisher update into `main`. Keep shared docs
   action versions aligned in both workflows when updating the publisher.
2. Wait for **Publish Docs (Staging)** to succeed and review the QA site.
3. Copy **Published commit** from that staging run's summary.
4. Open **Actions → Publish Docs (Production) → Run workflow**, select `main`,
   and enter that full commit SHA as `publish_ref`.
5. Run the workflow to publish the reviewed source to the public site. This does
   not run the product release pipeline or create a GitHub Release.

Both workflows also support manual publication of a chosen source revision. Use
the same immutable commit SHA for staging and production rather than a moving
branch name, and review the QA run again after any publisher changes. Each run's
summary records the actual checked-out commit and its destination URL.

### AWS Roles

The role name is the same, but the AWS accounts and role ARNs are different:

- QA: `arn:aws:iam::529607359382:role/Proj-GHA-open-perception-kit-s3-upload`.
- Production: `arn:aws:iam::930800959236:role/Proj-GHA-open-perception-kit-s3-upload`.

Both workflows use `eu-west-1` and verify that the credentials belong to their
expected account. Authentication uses GitHub OIDC. QA must trust `main`;
production must trust `main` for manual publication, the release workflow's ref,
and release-tag subjects for direct release events.

### Dependencies

- Repository inputs: `docs/public/`, including `docs-config.json` and `static/`,
  and `scripts/private/inject_docs_build_info.py`. The config and helper already
  match the source repository; no duplicate copies are needed.
- Actions: `actions/checkout` v7, `ARM-software/docs-action` v0.5.0
  (`validate` and `publish`), and `aws-actions/configure-aws-credentials` v6.
- The shared docs actions install Node.js 24, pnpm 10.33.3, and
  `arm-docs-github-action=0.9.0` from `https://artifacts.tools.arm.com/tools-deb`.
  The publisher package supplies the shared site generator. The job does not use
  the local preview image or `scripts/gen-doc.sh`.
- AWS: the separate QA and production roles described above. Publishing
  destinations come from the corresponding shared action preset.

### Validate Without Publishing

With the Node.js, pnpm, and publisher versions above installed, run from the
repository root:

```bash
arm-docs validate --docs-root docs/public --site-id open-perception-kit
```

This builds the site without AWS credentials or uploads. `--docs-root` must be
relative to the repository root.

## Asset Paths

Put images referenced by public Markdown pages in `docs/public/assets` and use paths relative to each Markdown file. The Arm publisher bundles these images, while VS Code Markdown preview and plain HTML generated by `./scripts/gen-doc.sh` resolve the same paths directly.

Reserve `docs/public/static` for assets served at site-root URLs. The publisher relocates this directory, so public Markdown must not use `./static/...` or `../static/...` image paths. Developer-facing architecture pages can still reference generated diagrams in `docs/public/static/img` using file-relative paths; these pages are outside the published docs root.

For example, in `docs/public/how-to/performance-measurement.md`:

```md
![Performix results](../assets/performix-results.jpg)
```

For generated diagrams, put the `.puml` source in `docs/plantuml` and run `./scripts/gen-doc.sh`.

See [the docs system docs](https://qa.developer.arm.com/dev-docs/arm-docs-github-action/) for more information.
