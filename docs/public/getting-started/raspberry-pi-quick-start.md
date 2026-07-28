---
title: Raspberry Pi 5 Tutorial
sidebar_position: 1
sidebar_label: Raspberry Pi 5
description: Run Perception XPK on a Raspberry Pi 5 target with VS Code, Dev Containers, and the browser viewer.
---

# Raspberry Pi 5 Tutorial

Use this tutorial when PEK will run on a Raspberry Pi 5. Your normal computer is used to connect with VS Code. The build, container, and pipeline run on the Raspberry Pi.

This page follows the intended first-user path:

1. Check the hardware.
2. Flash Raspberry Pi OS and enable SSH.
3. Connect to the Pi.
4. Install Pi prerequisites.
5. Clone PEK on the Pi.
6. Open PEK from VS Code.
7. Prepare the Dev Container.
8. Build and run the first pipeline.
9. See first inference in the browser.
10. Try a camera source after first success.

## 1. Check The Hardware

You need:

- Raspberry Pi 5.
- Raspberry Pi OS based on Debian Trixie.
- Network connection between your normal computer and the Raspberry Pi.
- Power supply suitable for Raspberry Pi 5 and attached hardware.
- Optional camera. The first run uses checked-in sample media, so the camera is not required for first success.

## 2. Flash Raspberry Pi OS And Enable SSH

The best way to enable SSH is during imaging. That lets you control the Pi from your normal computer without connecting a monitor and keyboard.

Follow [Raspberry Pi SSH Setup](raspberry-pi-ssh.md) before you continue if SSH is not already working.

Expected result: this command works from your normal computer in the **host shell**:

```bash
ssh <username>@raspberrypi.local
```

If `raspberrypi.local` does not work, use the Raspberry Pi IP address:

```bash
ssh <username>@<raspberry-pi-ip-address>
```

## 3. Connect To The Raspberry Pi

Run on your normal computer in the **host shell**:

```bash
ssh <username>@raspberrypi.local
```

After login, you are in the **Raspberry Pi shell**. The next commands run on the Pi.

## 4. Update The Pi And Install Packages

Run in the **Raspberry Pi shell**:

```bash
sudo apt update
sudo apt full-upgrade -y
sudo rpi-eeprom-update -a
```

Follow the link below to install Docker: 

* [Debian Installation Guide](https://docs.docker.com/engine/install/debian/)

Install both Docker Engine and the Docker Compose plugin from the Debian guide so `docker compose` is available for later steps.

Also install GitHub CLI. Authenticate it with read access
to the pinned `Arm-Debug/modelfetch` release. The repository is internal, so
its release asset is not available anonymously:

```bash
gh auth login --hostname github.com
gh auth status --hostname github.com
```

If the upgrade asks for a reboot, reconnect with SSH afterwards.

## 5. Run A Preflight Check

Run in the **Raspberry Pi shell**:

```bash
docker info
docker --version
docker compose version
gh --version
gh auth status --hostname github.com
```

If `docker info` fails with a permission error, add your user to the `docker` group and log out and back in:

```bash
sudo usermod -aG docker "$USER"
```

Expected result: Docker Engine and Docker Compose both print version information.

If these checks fail, fix them before opening the project in VS Code. The Dev Container depends on the Pi host setup.

After PEK is cloned in the next step, the repository also contains `./scripts/pre-req.sh`. Treat it as an extra helper check, not as a replacement for the checks above.

## 6. Clone PEK On The Raspberry Pi

Run in the **Raspberry Pi shell**:

```bash
git clone https://github.com/Arm-Debug/amp-dev-forge.git
cd amp-dev-forge
```

HTTPS cloning is the simplest first path. If you must clone with SSH, use [GitHub SSH Key Setup](github-ssh-key.md).

Until PEK is not released to a public repository, the SSH method has to be used:

```bash
git clone git@github.com:Arm-Debug/amp-dev-forge.git

```

Expected result: the `pek` folder exists on the Raspberry Pi.

The checked-in YOLOv11 quick start needs no Hugging Face credential. For a
private or gated model, export `HF_TOKEN` in the Pi login environment used by
VS Code Remote SSH, then reconnect VS Code to the Pi:

```bash
touch ~/.profile &&
  chmod 600 ~/.profile &&
  printf '%s\n' 'export HF_TOKEN="hf_..."' >> ~/.profile
```

The owner-only permission keeps the persisted credential private. The Dev
Container passes the value unchanged to modelfetch.

## 7. Check VS Code Prerequisites On Your Computer

On your normal computer, install:

- Visual Studio Code.
- VS Code **Remote - SSH** extension.
- VS Code **Dev Containers** extension.

These extensions are required before you connect to the Pi with VS Code.

## 8. Open The Pi From VS Code

On your normal computer, open VS Code.

1. Open the Command Palette.
   - Windows/Linux: `Ctrl+Shift+P`.
   - macOS: `Cmd+Shift+P`.
2. Run **Remote-SSH: Connect to Host...**.

![VS Code Remote SSH open remote window command](/img/16-open-remote-window.png)

3. Choose or enter:

```text
<username>@raspberrypi.local
```

If `.local` did not work in the terminal, use the IP address instead:

```text
<username>@<raspberry-pi-ip-address>
```

![VS Code SSH host selection](/img/18-select-ssh-configuration.png)

Expected result: VS Code opens a remote window connected to the Raspberry Pi.

## 9. Open The PEK Folder And Prepare The Container

In the VS Code remote window:

1. Open the `pek` folder on the Raspberry Pi.

![VS Code opening the PEK folder on the Raspberry Pi](/img/19-reopen-folder.png)

2. Open the Command Palette.
3. Run **Dev Containers: Reopen in Container**.

![VS Code reopening the Raspberry Pi project in a Dev Container](/img/20-reopen-in-container.png)

4. Choose **Raspberry Pi 5 perception-experience-kit**.

VS Code may say that it is building the container. Think of this as preparing the PEK environment. It can take several minutes on the first run.

Expected result: VS Code reloads and opens the repository inside the Dev Container. A new VS Code terminal is now the **Docker shell on the Raspberry Pi**.

## 10. Build The Project

Run in the **Docker shell on the Raspberry Pi**:

```bash
./scripts/build-elements.sh debug false
```

You can also use the VS Code task:

1. Open the Command Palette.
2. Run **Tasks: Run Task**.
3. Choose **00 Build Project**.

![VS Code build task for PEK](/img/08-build-project.png)

Expected result: the build finishes without errors and `tools/pek-menu` exists.

## 11. Run The First Pipeline

A pipeline is a saved runtime preset. It tells PEK where the input comes from, which models can run, and where the result is shown.

Run in the **Docker shell on the Raspberry Pi**:

```bash
./tools/pek-menu 05-full-onnx-raspicam
```

For a USB camera exposed as `/dev/video0`, run `./tools/pek-menu 06-full-onnx-usb-cam` instead. Use `./tools/pek-menu 01-full-onnx` when you want the bundled video-file source.

Leave this terminal open. The pipeline is running while this command is active.

Expected result: PEK starts the selected ONNX pipeline.

![PEK pipeline selection view](/img/09-select-pipeline.png)

## 12. Open The Web UI

Open a browser on your normal computer:

```text
http://raspberrypi.local:9999
```

If that does not work, use the Pi IP address:

```text
http://<raspberry-pi-ip-address>:9999
```

In the **AI Models** panel, enable one model first. Start with `yolov11` or `mobilenetv2`.

![PEK browser UI after opening the web view](/img/10-browser-ui.png)

Expected result: the page shows the PEK view and enabling a model produces an overlay or result. If you chose `05-full-onnx-raspicam` or `06-full-onnx-usb-cam`, the browser shows live camera input.

## 13. Switch From Sample Media To Camera

The checked-in camera presets use live camera sources by default:

- `05-full-onnx-raspicam` uses the Raspberry Pi camera source.
- `06-full-onnx-usb-cam` uses the USB camera source at `/dev/video0`.

For the full camera walkthrough, use [Use A Camera](../how-to/camera-input.md).

Use the manual source-editing path below only when your camera device, camera name, resolution, or pipeline preset needs to differ from those defaults.

Open the pipeline file you want to adapt and replace the first source lines in the `pipeline` array.

The checked-in sample source currently starts like this:

```json
"filesrc location=/work/data/videos/GettyImages-1140581459.mov !",
"decodebin !",
"videoconvert !",
"video/x-raw,format=BGRA !",
```

For a USB camera, check the device path in the **Raspberry Pi shell**:

```bash
v4l2-ctl --list-devices
```

If the camera is `/dev/video0`, replace the source lines with:

```json
"v4l2src device=/dev/video0 ! \"image/jpeg,width=1280,height=720,framerate=30/1\" !",
"jpegdec !",
"videoconvert ! video/x-raw,format=BGRA !",
```

For a CSI camera, check the camera name in the **Raspberry Pi shell**:

```bash
rpicam-hello --list-cameras
```

Then use the full camera name in a source block like this:

```json
"libcamerasrc camera-name=\"/base/axi/pcie@1000120000/rp1/i2c@80000/imx708@1a\" !",
"video/x-raw,format=RGB,width=1536,height=864,framerate=60/1 !",
"videoconvert ! video/x-raw,format=BGRA !",
```

Keep the rest of the pipeline unchanged for the first camera test.

The pipeline files also contain these alternative camera sources as templates:

- `alternative-source-usbcam` for USB cameras.
- `alternative-source-raspicam` for Raspberry Pi CSI cameras.

Expected result: after you rerun `pek-menu`, the browser shows camera input.

## 14. Stop And Run Again

To stop PEK, click the terminal that is running the pipeline and press `Ctrl+C`.

To run the last selected pipeline again, run in the **Docker shell on the Raspberry Pi**:

```bash
./tools/pek-menu -l
```

## If Something Fails

- If terminal SSH fails, return to [Raspberry Pi SSH Setup](raspberry-pi-ssh.md).
- If VS Code cannot connect over SSH, confirm terminal SSH works first.
- If `raspberrypi.local` does not resolve, use the Pi IP address.
- If the Dev Container does not start, confirm Docker works on the Raspberry Pi with `docker info`.
- If the browser opens but no result appears, enable a model in the **AI Models** panel.
- If you expected a live camera feed, use `05-full-onnx-raspicam` for a Raspberry Pi camera or `06-full-onnx-usb-cam` for a USB camera at `/dev/video0`, then follow the camera section above if your device needs custom source settings.

[Back to Get Started](/getting-started)
