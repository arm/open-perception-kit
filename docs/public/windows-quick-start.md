---
sidebar_position: 3
sidebar_label: Windows
---

# Windows Quick Start

Use this guide on a Windows computer. PEK runs inside WSL and a VS Code Dev Container, so most commands are Linux commands even though your computer is Windows.

## What You Need

Install these before you start:

- WSL with Ubuntu installed.
- Git inside WSL.
- Docker Desktop with WSL integration enabled.
- Visual Studio Code on Windows.
- VS Code **Dev Containers** extension.
- Optional: VS Code **WSL** extension.

If you want to use a USB camera from WSL later, you may also need USBIPD or WSL USB Manager. You do not need that for the first sample-media run.

## 1. Open Ubuntu/WSL

Open the Windows Start menu and start **Ubuntu**.

All commands in this section run in the **WSL shell**.

Check that basic tools are available:

```bash
git --version
docker --version
docker compose version
code --version
```

If `docker` does not work, open Docker Desktop and confirm that WSL integration is enabled for your Ubuntu distribution.

## 2. Get The Repository

The easiest path is HTTPS cloning. It does not require an SSH key.

Run in the **WSL shell**:

```bash
git clone https://github.com/Arm-Debug/pek.git
cd pek
```

If you must clone with SSH, set up your key first: [GitHub SSH Key Setup](github-ssh-key.md).

Expected result: you are in the `pek` folder in WSL.

## 3. Open The Project In VS Code

Run in the **WSL shell**, from the `pek` folder:

```bash
code .
```

VS Code should open the folder through WSL. In VS Code:

![VS Code opened in the PEK repository](../static/img/04-starting-point-vscode.png)

1. Open the Command Palette with `Ctrl+Shift+P`.
2. Run **Dev Containers: Reopen in Container**.

![VS Code command palette showing Reopen in Container](../static/img/05-reopen-in-container.png)

3. Choose **PC perception-experience-kit**.

![VS Code Dev Container selection dialog](../static/img/06-reopen-in-container2.png)

4. Wait for the container to finish building.

Expected result: VS Code reloads and the lower-left corner shows that you are inside the Dev Container.

![VS Code terminal inside the Dev Container](../static/img/07-in-container-new-console.png)

## 4. Build PEK

Open a new terminal in VS Code after the container is ready. This terminal is the **Docker shell**.

Run in the **Docker shell**:

```bash
./scripts/build-elements.sh debug false
```

You can also use the VS Code task:

1. Open the Command Palette with `Ctrl+Shift+P`.
2. Run **Tasks: Run Task**.
3. Choose **00 Build Project**.

![VS Code build task for PEK](../static/img/08-build-project.png)

Expected result: the build finishes without errors and `tools/pek-menu` exists.

## 5. Start The First Pipeline

Run in the **Docker shell**:

```bash
./tools/pek-menu 01-full-onnx
```

You can also use the VS Code task **00 Run project and select pipeline** and choose `01-full-onnx`.

![PEK pipeline selection view](../static/img/09-select-pipeline.png)

Expected result: the pipeline starts and keeps running in the terminal. Leave that terminal open.

Some GStreamer or browser-connection warnings can appear while the pipeline is running. Treat the browser result in the next step as the real success check.

## 6. Open The Web UI

Open Microsoft Edge or Firefox on Windows:

```text
http://localhost:9999
```

In the **AI Models** panel, enable one model first. For example, enable `yolov11` or `mobilenetv2`.

![PEK browser UI after opening the web view](../static/img/10-browser-ui.png)

Expected result: the page shows the PEK view and enabling a model produces an overlay or result. The default quick-start pipeline uses checked-in sample media; `06-full-onnx-usb-cam` uses a USB camera at `/dev/video0`.

## 7. Stop And Run Again

To stop PEK, click the terminal that is running the pipeline and press `Ctrl+C`.

To run the last selected pipeline again, run in the **Docker shell**:

```bash
./tools/pek-menu -l
```

## If Something Fails

- If VS Code says the container cannot start, make sure Docker Desktop is open.
- If Docker commands fail in WSL, check Docker Desktop WSL integration.
- If the browser opens but no result appears, enable a model in the **AI Models** panel.
- If you see a path error, confirm that VS Code opened the repository folder, not its parent folder.

[Back to README](../../README.md)
