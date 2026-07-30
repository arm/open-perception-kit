---
title: macOS Quick Start
sidebar_position: 4
sidebar_label: macOS
description: Set up Perception XPK on macOS with Docker Desktop and a local VS Code Dev Container.
---

# macOS Quick Start

Use this guide on a Mac. PEK runs inside a VS Code Dev Container. The first run uses checked-in sample media and the local browser UI.

## What You Need

Install these before you start:

- Git.
- Docker Desktop.
- Visual Studio Code.
- VS Code **Dev Containers** extension.

Docker Desktop is the recommended path for this quick start.

## 1. Check The Tools

Run in the **host shell**:

```bash
git --version
docker --version
docker compose version
code --version
```

If Docker commands fail, open Docker Desktop and wait until it says Docker is running.

## 2. Get The Repository

The easiest path is HTTPS cloning. It does not require an SSH key.

Run in the **host shell**:

```bash
git clone https://github.com/Arm-Debug/amp-dev-forge.git
cd amp-dev-forge
```

If you must clone with SSH, set up your key first: [GitHub SSH Key Setup](github-ssh-key.md).

Expected result: you are in the `pek` folder.

## 3. Open The Project In VS Code

Run in the **host shell**, from the `pek` folder:

```bash
code .
```

If the build needs private or gated models, export a read-only `HF_TOKEN`
before opening VS Code:

```bash
export HF_TOKEN="hf_your_token_here"
code .
```

Docker uses the token only while downloading the pinned model files into the
image. It is not added to the runtime container environment. After correcting
a token, run **Dev Containers: Rebuild Container**; initialization refreshes
the model-download cache key.

In VS Code:

![VS Code opened in the PEK repository](/img/04-starting-point-vscode.png)

1. Open the Command Palette with `Cmd+Shift+P`.
2. Run **Dev Containers: Reopen in Container**.

![VS Code command palette showing Reopen in Container](/img/05-reopen-in-container.png)

3. Choose **PC perception-experience-kit**.

![VS Code Dev Container selection dialog](/img/06-reopen-in-container2.png)

4. Wait for the container to finish building.

Expected result: VS Code reloads into the Dev Container.

![VS Code terminal inside the Dev Container](/img/07-in-container-new-console.png)

## 4. Build PEK

Open a new terminal in VS Code after the container is ready. This terminal is the **Docker shell**.

Run in the **Docker shell**:

```bash
./scripts/build-elements.sh debug false
```

You can also use the VS Code task **00 Build Project**.

![VS Code build task for PEK](/img/08-build-project.png)

Expected result: the build finishes without errors and `tools/pek-menu` exists.

## 5. Start The First Pipeline

Run in the **Docker shell**:

```bash
./tools/pek-menu 01-full-onnx
```

You can also use the VS Code task **00 Run project and select pipeline** and choose `01-full-onnx`.

![PEK pipeline selection view](/img/09-select-pipeline.png)

Expected result: the pipeline starts and keeps running in the terminal. Leave that terminal open.

## 6. Open The Web UI

Open Safari, Microsoft Edge, or Firefox:

```text
http://localhost:9999
```

In the **AI Models** panel, enable one model first. For example, enable `yolov11` or `mobilenetv2`.

![PEK browser UI after opening the web view](/img/10-browser-ui.png)

Expected result: the page shows the PEK view and enabling a model produces an overlay or result. The default quick-start pipeline uses checked-in sample media, not a live camera.

## 7. Stop And Run Again

To stop PEK, click the terminal that is running the pipeline and press `Control+C`.

To run the last selected pipeline again, run in the **Docker shell**:

```bash
./tools/pek-menu -l
```

## If Something Fails

- If the container cannot start, check that Docker Desktop is running.
- If the browser cannot connect, confirm the pipeline is still running in the Docker shell.
- If the browser opens but no result appears, enable a model in the **AI Models** panel.
- If you connect from this Mac to a Raspberry Pi later, allow VS Code local network access in macOS **Settings > Privacy & Security > Local Network**.

[Back to Get Started](/getting-started)
