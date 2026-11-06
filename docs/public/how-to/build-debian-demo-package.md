---
title: Build the Debian demo package
sidebar_label: Debian demo package
description: Package the OPK launcher, configuration, resolved models, and demonstration media.
---

# Build the Debian demo package

The architecture-specific `opk-demo` package complements `opk-runtime` with
`opk-menu`, model descriptors and resolved model binaries, OpChains, pipeline
presets, schemas, model-local Python scripts, and demonstration media. It
depends on the exact matching `opk-runtime` version and Debian revision.

Model and media downloads retain their existing owners. Prepare the model tree
with `opk-models` or `scripts/download-models.py`, and prepare demo videos with
`opk-demo-media` or `scripts/private/download-demo-videos.sh`. Package assembly
only consumes those resolved outputs and fails when a descriptor's local model
file is missing.

Configure and build the package inside the supported development container:

```bash
meson setup development/build-demo-deb development \
  --buildtype=release \
  -Ddemo_deb_package=true \
  -Ddeb_package_revision=1

meson compile -C development/build-demo-deb opk-demo-deb
```

The output is architecture-specific because it contains `opk-menu`:

```text
development/build-demo-deb/packaging/opk-demo_0.3.1-1_amd64.deb
```

Install both packages into Debian Trixie:

```bash
apt install \
  ./opk-runtime_0.3.1-1_amd64.deb \
  ./opk-demo_0.3.1-1_amd64.deb

opk-menu
```

Installed content lives under `/usr/share/opk`. The packaged launcher uses
that directory by default, while `OPK_PROJECT_ROOT`, `OPK_PLUGIN_PATH`, and
`OPK_OPS_PATH` remain available as explicit overrides. `OPK_PLUGIN_PATH`
selects the GStreamer plugin directory and `OPK_OPS_PATH` selects the private
OPK operation-module directory. Mutable menu selection state follows
`OPK_STATE_DIR`, then `XDG_STATE_HOME`, and otherwise uses
`$HOME/.local/state/opk` instead of writing into packaged content.

For a non-interactive installation smoke test, resolve a packaged preset
without starting its pipeline:

```bash
opk-menu --dry-run testing/only-opkmenu
```
