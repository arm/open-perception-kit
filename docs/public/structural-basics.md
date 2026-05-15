---
sidebar_position: 10
sidebar_label: Structural Basics
---

# Structural Basics

This page explains where users usually need to put their own files and which folders matter for day-to-day use.

## What will you learn from this documentation?

If you follow this page successfully, you will learn where PEK expects models, opchains, pipeline presets, media files, scripts, and source changes to live.

At the end of this page, you should be able to place new files in the right folders and tell when a task can stay in `config/` versus when it must move into `development/`.

## The folders most users need

### `config/`
This is the most important folder for normal usage.

- `config/models/` stores model folders. Put your model file, `model.json`, basic and minimal `opchain.json`, and `README.md` here. Runtime-specific compiled variants also live here, for example `mobilenetv2-hailo8/` and `mobilenetv2-hailo10/`.
- `config/opchains/` stores reusable multi-stage pipelines, for example detector + secondary model chains.
- `config/pipelines/` stores the top-level presets shown by `pek-menu`.

### `data/`
Use this for your own test media.

- `data/images/` is the easiest place for still-image tests.
- `data/videos/` is the easiest place for video-file tests.

### `docs/` and `docs1/`
These contain the project documentation.

- `docs/public/` contains the Markdown source used by the current docs site.
- `docs/static/` contains the images used by the docs.
- `docs1/` contains the flatter Markdown documentation set.

### `scripts/`
This contains the main helper scripts you are expected to run.

- `scripts/build-elements.sh` builds the runtime.
- `scripts/serve-docs.sh` and `scripts/serve-docs-plain.sh` serve the docs.
- `scripts/gen-doc.sh` refreshes generated documentation.
- `scripts/docker-nuke.sh` stops and removes Docker containers created by the project.
- `scripts/private/` contains internal helper scripts. Most users do not need to call them directly.

### `tools/`
This contains helper tools created or used by the project.

- `tools/pek-menu` is the launcher used to run pipeline presets. The VS Code run tasks call it for normal launches from the host side container or remote host container.

## When you need the source tree

Many users will not need to touch `development/`, but this depends on whether an existing postprocessor already fits the model output.

If the model can reuse an existing parser, you can usually stay in `config/`.
If the model needs a new parser or output handling logic, you will also need to work in `development/`, most often under `development/ops-std/postproc/` and the related postprocess registration.

## What users normally change

For normal bring-your-own-content work, the usual edit points are:

1. `config/models/` for your own models
2. `config/pipelines/` for your own input/output presets
3. `data/images/` and `data/videos/` for your own media
4. `README.md` files in the matching config folders when you want to document your addition

If no existing postprocessor matches your model output, add this to the list:

5. `development/ops-std/postproc/` for a new parser and the matching postprocess registration

## The normal runtime path

The usual flow is:

1. select a pipeline preset from `config/pipelines/`
2. that preset starts a GStreamer pipeline
3. `pekinfer` loads an OpChain from `config/opchains/` or `config/models/*/opchain.json`
4. the OpChain loads one or more model descriptors from `config/models/`
5. the result is shown or published by the downstream elements

You only need the deeper `development/` source tree if this flow is not enough for your use case or if your model output needs a new postprocessor.

## Common VS Code tasks

The Dev Container provides VS Code tasks for normal development:

- **00 Build Project** builds the runtime.
- **00 Run project with menu** opens the interactive `pek-menu` pipeline list.
- **00 Run project and select pipeline** prompts for a pipeline and runs it directly.
- **00 Run project with latest pipeline** reruns the last selected pipeline.
- **01 Clean Project** cleans build artifacts.
- **02 Build Tests** builds with tests enabled.
- **03 Run Tests** runs the test suite.

These tasks run inside the active Dev Container. If you run the equivalent commands manually, use the **Docker shell**.

## Quality checks

The container installs `expkits-ci`, which is used for many local and CI checks.

Run this in the **Docker shell** for help:

```bash
expkits-ci --help
```

You can also run the configured pre-commit checks manually:

```bash
pre-commit run --all-files
```

`pre-commit run` checks only staged files by default. Use `--all-files` when you want to check the whole working tree.

## What should you have at the end of this document?

By the end of this page, you should have:

- a practical map of the folders that matter for normal PEK work
- a clear understanding of the usual edit points for models, pipelines, and media
- a simple rule for when source-code changes are actually needed

Success looks like this: you can decide where to add a model, where to edit a pipeline, and whether your task stays in configuration or requires runtime code changes.

[Back to README](../../README.md)
