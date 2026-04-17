---
sidebar_position: 7
sidebar_label: TL;DR macOS
---

# macOS TL;DR

This is the shortest path from cloning the repository to running the first AMP pipeline on macOS.

Use this page if you want the quickest first run.
Use the other how-to pages if you want setup details, troubleshooting help, or deeper explanations.

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

## 3. Clone the repository

```bash
git clone git@github.com:Arm-Debug/amp-dev-forge.git
cd amp-dev-forge
```

## 4. Open the repository in VS Code

Open the cloned folder in VS Code.

Then run:
- "Reopen in Container"
- choose "PC amp-dev-forge"

Wait until the Dev Container finishes building.

## 5. Build the project

Use the build task in VS Code:
- run **00 Build Project**
- choose `debug` unless you specifically want `release`

Or build in the container terminal:

```bash
./scripts/build-elements.sh debug false
```

## 6. Start AMP

Run:

```bash
./tools/amp-menu
```

## 7. Run the first pipeline

In `amp-menu`, select:
- `01-full-onnx.json`

This is the shortest recommended first pipeline.

## 8. Open the web UI

Open:
- http://localhost:9999

Documentation is available at:
- http://localhost:8080

## 9. Run it again later without the menu

After you have selected a pipeline once, you can rerun the last selection with:

```bash
./tools/amp-menu -l
```

## If you want the deeper guides

Continue with the [main how-to guide](../deep-dives/how-to.md).
