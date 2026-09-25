---
title: Windows Quick Start
sidebar_position: 2
sidebar_label: Windows
description: Set up Open Perception Kit on Windows with WSL and Docker Desktop from the command line or VS Code.
---

# Windows Quick Start

Use this guide on a Windows computer. OPK runs in a Docker container managed
from WSL. You can use the WSL command line only or optionally work through a VS
Code Dev Container.

## What You Need

Install these before you start:

- WSL with Ubuntu installed.
- Git inside WSL.
- Docker Desktop with WSL integration enabled.

For the optional VS Code workflow, also install:

- Visual Studio Code on Windows.
- VS Code **Dev Containers** extension.
- VS Code **WSL** extension.

If you want to use a USB camera from WSL later, you may also need USBIPD or WSL USB Manager. You do not need that for the first sample-media run.

## 1. Open Ubuntu/WSL

Open the Windows Start menu and start **Ubuntu**.

All commands in this section run in the **WSL shell**.

Check that basic tools are available:

```bash
git --version
docker --version
docker compose version
docker info
```

If `docker` does not work, open Docker Desktop and confirm that WSL integration is enabled for your Ubuntu distribution or make sure docker CLI is installed in WSL.

For the optional VS Code workflow, also check:

```bash
code --version
```

### Configure mirrored WSL networking for WebRTC

Open `%UserProfile%\.wslconfig` from Windows and ensure it contains:

```ini
[wsl2]
networkingMode=mirrored

[experimental]
hostAddressLoopback=true
```

Apply the change from Windows PowerShell:

```powershell
wsl --shutdown
```

Then reopen Ubuntu/WSL before continuing. OPK's container initialization checks
this requirement and reports the same remediation if mirrored networking is
missing host-address loopback.

## 2. Get The Repository

The easiest path is HTTPS cloning. It does not require an SSH key.

Run in the **WSL shell**:

```bash
git clone https://github.com/arm/open-perception-kit.git
cd open-perception-kit
```

If you must clone with SSH, set up your key first: [GitHub SSH Key Setup](github-ssh-key.md).

Expected result: you are in the `open-perception-kit` folder in WSL.

### Optional private or gated model access

Accessible public models download anonymously. If the build also needs private
or gated models, export a read-only Hugging Face token in the WSL shell before
starting either workflow:

```bash
export HF_TOKEN="hf_your_token_here"
```

Docker supplies the token only to the model-download build step. It is not
added to the runtime container environment. Failed model downloads are logged
and skipped, so the image can still build.

You can add this line to the .bashrc of your user, so the token will be 
automatically added at the start of the shell.

## 3. Command-Line-Only Workflow

Run these commands in the **WSL shell**, from the repository folder:

```bash
./scripts/quick_start.sh
./scripts/build.sh
./scripts/run.sh
```

The scripts create or reuse the development container, build OPK inside it,
and run the bundled `yolo26n-320` sample. Keep the last command running and
continue to [Open The Web UI](#7-open-the-web-ui).

Expected result: the build prints
`Pipeline launcher is ready at /work/tools/opk-menu`, then the run command
prints a `gst-launch-1.0` command and keeps running.

To open an interactive shell inside the same container, run:

```bash
./scripts/enter_cli.sh
```

This is optional; `build.sh` and `run.sh` work directly from the WSL shell.

## 4. Open The Project In VS Code

Skip this section if you used the command-line-only workflow.

Run in the **WSL shell**, from the `open-perception-kit` folder:

```bash
code .
```

VS Code should open the folder through WSL. In VS Code:

![VS Code opened in the OPK repository](/img/04-starting-point-vscode.png)

1. Open the Command Palette with `Ctrl+Shift+P`.
2. Run **Dev Containers: Reopen in Container**.

![VS Code command palette showing Reopen in Container](/img/05-reopen-in-container.png)

3. Choose **PC open-perception-kit**.

![VS Code Dev Container selection dialog](/img/06-reopen-in-container2.png)

4. Wait for the container to finish building.

Expected result: VS Code reloads and the lower-left corner shows that you are inside the Dev Container.

![VS Code terminal inside the Dev Container](/img/07-in-container-new-console.png)

## 5. Build OPK

Open a new terminal (either in VS Code after the container is ready or in WSL). This terminal is the **Docker shell**.

Run in the **Docker shell**:

```bash
./scripts/build.sh debug false
```

In VS Code you can also use the following task:

1. Open the Command Palette with `Ctrl+Shift+P`.
2. Run **Tasks: Run Task**.
3. Choose **00 Build Project**.

![VS Code build task for OPK](/img/08-build-project.png)

Expected result: the build finishes without errors and `tools/opk-menu` exists.

## 6. Start The First Pipeline

Run in the **Docker shell** (either in VSCode devcontainer or in WSL after entering with `enter_cli.sh`):

```bash
./tools/opk-menu yolo26n-320
```

You can also use the VS Code task **00 Run project and select pipeline** and choose `yolo26n-320`.

![OPK pipeline selection view](/img/09-select-pipeline.png)

Expected result: the pipeline starts and keeps running in the terminal. Leave that terminal open.

Some GStreamer or browser-connection warnings can appear while the pipeline is running. Treat the browser result in the next step as the real success check.

## 7. Open The Web UI

Open Microsoft Edge or Firefox on Windows:

```text
http://localhost:9999
```

In the **Model Selector** panel, enable a model to start inference.

![OPK browser UI after opening the web view](/img/10-browser-ui.png)

Expected result: the page shows the OPK view and enabling a model produces an overlay or result. The default quick-start pipeline uses checked-in sample media; `full-onnx-usb-cam` uses a USB camera at `/dev/video0`.

## 8. Stop And Run Again

To stop OPK, click the terminal that is running the pipeline and press `Ctrl+C`.

To run the last selected pipeline again, run in the **Docker shell**:

```bash
./tools/opk-menu -l
```

## If Something Fails

- If container initialization reports that host-address loopback is required,
  update `%UserProfile%\.wslconfig`, run **wsl --shutdown** from Windows
  PowerShell, and reopen WSL.
- If Docker reports that `/open-perception-kit` is already in use, check
  `docker ps -a --filter name='^/open-perception-kit$'`. If it is an old
  OPK container you no longer need, remove it with
  `docker rm --force open-perception-kit`, then rerun
  `./scripts/quick_start.sh`. This removes the container, not repository files.
- If VS Code says the container cannot start, make sure Docker Desktop is open.
- If Docker commands fail in WSL, check Docker Desktop WSL integration.
- If the browser opens but no result appears, enable a model in the **Model Selector** panel.
- If you see a path error, confirm that VS Code opened the repository folder, not its parent folder.

[Back to Get Started](/getting-started)
