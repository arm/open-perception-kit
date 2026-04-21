---
sidebar_position: 2
sidebar_label: macOS Quick Guide
---

# macOS Quick-Guide

This is the shortest path from cloning the repository to running the first AMP pipeline on macOS.

Use this page if you want the quickest first run.
Use the other how-to pages if you want setup details, troubleshooting help, or deeper explanations.

## What will you learn from this documentation?

If you follow this page successfully, you will learn how to:

- prepare a supported macOS host for AMP development
- open the repository in the expected container workflow
- build the project and start `amp-menu`
- run the first pipeline and verify that the local web UI is reachable

At the end of this guide, you should have AMP running on your desk on a macOS development machine, with the first pipeline launched and the web UI available at `http://localhost:9999`.

## 1. Install the host tools

Install:
- Git
- Docker Desktop
- Visual Studio Code
- VS Code Dev Containers extension

> Colima is not the recommended path here.
> The existing project docs treat Docker Desktop as the expected setup.

## 2. Make sure your Git SSH key is ready

If you clone with SSH, start `ssh-agent` and add your key first.

```bash
eval "$(ssh-agent -s)"
ssh-add <your-private-key>
```

> Expected result: Git operations over SSH should work without prompting for a password on every repository access.

## 3. Clone the repository

```bash
git clone git@github.com:Arm-Debug/amp-dev-forge.git
cd amp-dev-forge
```

> Expected result: the `amp-dev-forge` folder exists locally and VS Code can open it.

## 4. Open the repository in VS Code

Open the cloned folder in VS Code. Either in the UI or with the following command:

```bash
code .
```

Then run:
- "Reopen in Container"
- choose "PC amp-dev-forge"

Wait until the Dev Container finishes building.

> Expected result: VS Code reconnects into the container and the project opens with the container environment active.

## 5. Build the project

Use the build task in VS Code:
- run **00 Build Project**
- choose `debug` unless you specifically want `release`

Or build in the container terminal:

```bash
./scripts/build-elements.sh debug false
```

> Expected result: the build completes successfully and `tools/amp-menu` is available.

## 6. Start AMP

Run:

```bash
./tools/amp-menu
```

Stop:

To stop an application that was not started from a VS Code launch configuration, press Control+C in the console.

> Expected result: `amp-menu` starts and shows the pipeline selection menu.

## 7. Run the first pipeline

In `amp-menu`, select:
- `01-full-onnx.json`

This is the shortest recommended first pipeline.

> Expected result: the selected pipeline launches and the web UI can later list the preset's models.

## 8. Open the web UI

> Disclaimer: Safari is the suggested browser for the AMP web UI on mac.

Open:
- http://localhost:9999

Documentation is available at:
- http://localhost:8080

In the **AI Models** panel, enable one or more models to start inference.
The main demo presets register their models as inactive by default so you can switch them on individually.

> Expected result: the AMP UI opens in your browser, the documentation endpoint is reachable, and enabled models begin producing overlays or results.

## 9. Run it again later without the menu

After you have selected a pipeline once, you can rerun the last selection with the -l (latest) argument:

```bash
./tools/amp-menu -l
```

> Expected result: AMP starts the most recently selected pipeline directly without showing the menu.

## If you want the deeper guides

Continue with the [main how-to guide](../deep-dives/index.md).

If you want a guided repository walk-through, continue with the [exercise quick guide](exercise.md).

## What should you have at the end of this document?

By the end of this guide, you should have:

- a working Dev Container for AMP on macOS
- a successful local build
- `amp-menu` starting correctly
- `01-full-onnx.json` running at least once
- the AMP web UI reachable at `http://localhost:9999`

Success looks like this: the container opens correctly, the build completes, the pipeline starts from `amp-menu`, and the browser can reach the AMP UI.
