<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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

Public documentation lives in `docs/public`; developer architecture documentation
lives in `docs/arch`. PlantUML sources live in `docs/plantuml`. The common generator
uses the pinned `opk-docs` container and the released Arm site renderer for local
viewing. Publication uses the existing Arm actions with the same prepared
Markdown and diagram assets. Generation never modifies tracked sources.

## Generate and view locally

Run these commands from the checkout on a host with Docker or Podman and Python 3:

```bash
./scripts/gen-doc.sh
./scripts/serve-docs.sh
```

Open [the local site](http://localhost:3003). It includes public and architecture
pages. Generation writes `docs/html`; serving only reads that output. After
editing Markdown, diagrams, styles, or images, rerun `gen-doc.sh` and refresh the
browser. There is no watcher or automatic generation. `serve-docs-plain.sh` serves
the same output on port 8080.

Initial toolchain setup and generation need network access, including access to
Arm's npm mirror by default. Serving the generated site needs no internet or Arm
credentials. Open generated HTML through localhost; opening HTML files directly
with `file://` is unsupported.
External links still require access to their destinations.

`OPK_PROJECT_ROOT` can select another checkout and must be an absolute path.
`DOCS_PORT` changes the serving port. `DOCS_IMAGE` selects an already-built
compatible documentation image; otherwise `gen-doc.sh` builds the checked-in
`opk-docs` target from the checkout containing the script. Set `NPM_REGISTRY` to
override the image's default Arm npm mirror,
for example `NPM_REGISTRY=https://registry.npmjs.org ./scripts/gen-doc.sh` outside
the Arm network.

### Optional API reference

```bash
./scripts/gen-doc.sh --with-api
```

This adds a linked Doxygen C++ API reference to the local site. The normal build
omits it, including after an earlier API build. API references and architecture
pages are not included in publication. This does not change OPK release assets.

## View Markdown in VS Code

Use **Markdown Preview Enhanced: Open Preview to the Side** to view source
Markdown with embedded PlantUML diagrams. Install the extension in your existing
VS Code environment and ensure Java, Graphviz, and `plantuml-mit-1.2026.2.jar`
are available there. The rich image already includes these tools, with the JAR
at `/opt/opk-deps/plantuml-mit-1.2026.2.jar`.

Set `markdown-preview-enhanced.plantumlJarPath` to the absolute JAR path in that
environment and leave `markdown-preview-enhanced.plantumlServer` empty. After
setup, rendering works offline and does not send diagrams to a server. Container
validation does not establish native Java/editor support on macOS or Windows.

Reference a shared diagram from an embedded block, relative to the Markdown file:

````markdown
```plantuml
@startuml
!include ../plantuml/component-overview.puml
@enduml
```
````

Keep diagram content and shared style in the `.puml` files. The site generator
renders these blocks to SVGs in its build copy. Do not commit generated diagram
images. Reload the Markdown preview after changing an included `.puml` file if
the editor retains a cached diagram.

GitHub rendering of embedded PlantUML is outside the current scope. Whether to
migrate to Mermaid or explicitly drop GitHub diagram viewing remains a separate
decision under EXPKITS-1554.

## Publish From GitHub Actions

| Workflow | Automatic trigger | Destination |
| --- | --- | --- |
| [Publish Docs (Staging)](../.github/workflows/docs-publish.yml) | Push to `main` | [QA site](https://qa.developer.arm.com/dev-docs/open-perception-kit/) |
| [Publish Docs (Production)](../.github/workflows/docs-publish-production.yml) | Release workflow | [Public site](https://developer.arm.com/dev-docs/open-perception-kit/) |

Both workflows support selecting a source revision manually. Production requires
that revision to be contained in `main`. Use an immutable commit SHA and review
its staging output before manually publishing it to production.

Prepare public Markdown and diagram assets for the existing publishing actions:

```bash
./scripts/gen-doc.sh --prepare-publish
```

This uses the small `opk-docs-prepare` image with Python, Java, Graphviz, and
PlantUML; it does not install the development or HTML renderer toolchains.
It writes `docs/prepared-public`, including rendered diagram SVGs and the
selected commit stamp, leaving local HTML output unchanged. It does not upload
anything. The workflows use preparation tools from their own workflow revision
against the selected source revision, including older commits whose scripts
predate this preparation mode. They pass the prepared directory to the existing
`docs-action/validate` and `docs-action/publish` actions. Those actions perform
their own HTML builds; the publisher owns the destination URL and analytics
settings. OPK consumes these released tools without modifying them.

The Release workflow supplies its exact release commit to the production docs
workflow. Existing release assets and their scope remain unchanged. Diagrams
come from that revision's PlantUML sources and the common pinned renderer.

AWS authentication remains GitHub OIDC in `eu-west-1`, using the existing roles:

- QA: `arn:aws:iam::529607359382:role/Proj-GHA-open-perception-kit-s3-upload`.
- Production: `arn:aws:iam::930800959236:role/Proj-GHA-open-perception-kit-s3-upload`.

Publication checks the expected account and retains the shared action's bucket
and CloudFront invalidation policy. Local generation and serving require no AWS
credentials.

## Asset paths and validation

Place authored public images under `docs/public/assets` and link to them relative
to the Markdown page. Reserve `docs/public/static` for site-root assets. Do not
reference `static` with relative Markdown image links: the Arm renderer relocates
that directory.

Source preparation checks referenced files and PlantUML rendering. Local site
generation also runs the Arm site's page/anchor validation and checks generated
links and assets. Publication uses the existing actions' validation and build.
A failed build leaves the last successful output available. Validation of
external websites is separate from offline viewing.

Generated sites include `source-revision.txt` with the checkout commit. The
local renderer is pinned to `arm-docs` 0.12.1; OPK copies its validated HTML from
an isolated temporary directory into `docs/html`.

The supported build environment is a Linux container on Linux, Windows with WSL,
macOS, or Raspberry Pi Linux. Test results must distinguish the host platform,
container architecture, and VS Code preview setup; an amd64 Linux run does not
verify the other platforms.
