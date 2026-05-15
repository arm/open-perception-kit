---
sidebar_position: 4
sidebar_label: Linux
---

# Linux Quick Start

Use this guide on a Linux computer. PEK runs inside a VS Code Dev Container, so build and run commands happen inside the container after setup.

## What You Need

Install these before you start:

- Git.
- Docker Engine.
- Docker Compose.
- Visual Studio Code.
- VS Code **Dev Containers** extension.
- `v4l-utils`, optional for the first run but useful for camera work.

On Ubuntu-like systems, run in the **host shell**:

```bash
sudo apt-get update
sudo apt-get install -y git docker.io docker-compose-plugin v4l-utils
```

Install VS Code from your normal package source if it is not already installed.

Check Docker in the **host shell**:

```bash
docker --version
docker compose version
docker info
```

If `docker info` fails with a permission error, add your user to the `docker` group and log out and back in:

```bash
sudo usermod -aG docker "$USER"
```

## 1. Get The Repository

The easiest path is HTTPS cloning. It does not require an SSH key.

Run in the **host shell**:

```bash
git clone https://github.com/Arm-Debug/pek.git
cd pek
```

If you must clone with SSH, set up your key first: [GitHub SSH Key Setup](github-ssh-key.md).

Expected result: you are in the `pek` folder.

## 2. Open The Project In VS Code

Run in the **host shell**, from the `pek` folder:

```bash
code .
```

In VS Code:

![VS Code opened in the PEK repository](../static/img/04-starting-point-vscode.png)

1. Open the Command Palette with `Ctrl+Shift+P`.
2. Run **Dev Containers: Reopen in Container**.

![VS Code command palette showing Reopen in Container](../static/img/05-reopen-in-container.png)

3. Choose **PC perception-experience-kit**.

![VS Code Dev Container selection dialog](../static/img/06-reopen-in-container2.png)

4. Wait for the container to finish building.

Expected result: VS Code reloads into the Dev Container.

![VS Code terminal inside the Dev Container](../static/img/07-in-container-new-console.png)

## 3. Build PEK

Open a new terminal in VS Code after the container is ready. This terminal is the **Docker shell**.

Run in the **Docker shell**:

```bash
./scripts/build-elements.sh debug false
```

You can also use the VS Code task **00 Build Project**.

![VS Code build task for PEK](../static/img/08-build-project.png)

Expected result: the build finishes without errors and `tools/pek-menu` exists.

## 4. Start The First Pipeline

Run in the **Docker shell**:

```bash
./tools/pek-menu 01-full-onnx
```

You can also use the VS Code task **00 Run project and select pipeline** and choose `01-full-onnx`.
On Linux with a USB camera exposed as `/dev/video0`, choose `06-full-onnx-usb-cam` for a live camera source enabled by default.

![PEK pipeline selection view](../static/img/09-select-pipeline.png)

Expected result: the pipeline starts and keeps running in the terminal. Leave that terminal open.

## 5. Open The Web UI

Open Microsoft Edge or Firefox:

```text
http://localhost:9999
```

In the **AI Models** panel, enable one model first. For example, enable `yolov11` or `mobilenetv2`.

![PEK browser UI after opening the web view](../static/img/10-browser-ui.png)

Expected result: the page shows the PEK view and enabling a model produces an overlay or result. The default quick-start pipeline uses checked-in sample media; `06-full-onnx-usb-cam` uses a USB camera at `/dev/video0`.

## 6. Stop And Run Again

To stop PEK, click the terminal that is running the pipeline and press `Ctrl+C`.

To run the last selected pipeline again, run in the **Docker shell**:

```bash
./tools/pek-menu -l
```

## If Something Fails

- If the container cannot start, check that `docker info` works in the host shell.
- If the build command is not found, confirm that you are in the Docker shell and in `/work`.
- If the browser opens but no result appears, enable a model in the **AI Models** panel.
- If you see a path error, confirm that VS Code opened the repository folder, not its parent folder.

[Back to README](index.md)
