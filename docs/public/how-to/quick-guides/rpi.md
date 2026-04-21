---
sidebar_position: 8
sidebar_label: Raspberry Pi Quick Guide
---

# Raspberry Pi Quick Guide

This is the shortest path from preparing the Raspberry Pi to running the first AMP pipeline.

Use this page if you want the quickest first run.
Use the longer Raspberry Pi and How-To pages if you want hardware setup details, camera setup details, or troubleshooting help.

## What will you learn from this documentation?

If you follow this page successfully, you will learn how to:

- prepare a Raspberry Pi 5 host for AMP work
- connect to the target from VS Code and reopen the repository in the container
- build AMP on the target and start `amp-menu`
- run a first pipeline and verify that the Pi-hosted web UI is reachable

At the end of this guide, you should have AMP running on your desk on a Raspberry Pi 5, with the first pipeline launched and the web UI available at `http://raspberrypi.local:9999`.

## 0. Required devices
For other possible hardware setups please check out the deep dive documentations.

- [Raspberry Pi 5 16GB](https://www.raspberrypi.com/products/raspberry-pi-5/)
- [AI HAT+](https://www.raspberrypi.com/products/ai-hat/)
- [Camera Module v3](https://www.raspberrypi.com/products/camera-module-3/)

> Expected result: you have the minimum supported Raspberry Pi hardware in place for the quick-start path.

## 1. Enable SSH access

Enable SSH on the Raspberry Pi and make sure you can connect to it from your development machine.

If needed, temporarily enable password authentication in `/etc/ssh/sshd_config`:

```ini
PasswordAuthentication yes
```

> Expected result: you can connect to the Raspberry Pi from your development machine over SSH.

## 2. Prepare the Raspberry Pi host

On the Raspberry Pi host, install the main packages:

```bash
sudo apt-get update
sudo apt-get install -y git docker.io v4l-utils libraspberrypi-bin
sudo apt-get install libcamera-apps libcamera-dev libcamera-doc libcamera-tools \
  gstreamer1.0-tools gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-gl \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev \
  gstreamer1.0-libcamera \
  ffmpeg \
  code cmake libcairo2-dev libssl-dev
```

If you use a Hailo NPU, also install:

```bash
sudo apt-get install hailo-all
```

## 3. Clone the repository on the Raspberry Pi

After SSH access is working, clone the repository on the Raspberry Pi:

```bash
git clone git@github.com:Arm-Debug/amp-dev-forge.git
cd amp-dev-forge
```

> Expected result: the repository is present on the Raspberry Pi and ready to be opened remotely from VS Code.

## 4. Open the Raspberry Pi in VS Code

From your development machine:
- connect to the Raspberry Pi over Remote SSH in VS Code
- open the cloned repository folder
- run "Reopen in Container"
- choose "RPI5 amp-dev-forge"

Wait until the Dev Container finishes building.

> Expected result: VS Code reconnects into the Raspberry Pi container and the project opens with the container environment active.

## 5. Build the project

Use the build task in VS Code:
- run **00 Build Project**
- choose `debug` unless you specifically want `release`

Or build in the container terminal:

```bash
./scripts/build-elements.sh debug false
```

> Expected result: the build completes successfully and `tools/amp-menu` is available on the Raspberry Pi.

## 6. Start AMP

Run:

```bash
./tools/amp-menu
```

Stop:

To stop an application that was not started from a VS Code launch configuration, press Ctrl+C(Windows) in the console.

> Expected result: `amp-menu` starts and shows the pipeline selection menu.


## 7. Run the first pipeline

For the shortest first run, select:
- `01-full-onnx.json`

If you specifically want the camera + Hailo path after that, use:
- `02-full-onnx-hailo.json`

> Expected result: the selected pipeline launches without errors and AMP begins running on the Raspberry Pi.

## 8. Open the web UI

> Disclaimer: Microsoft Edge is the suggested browser for the AMP web UI. If the image is not visible in the browser, open `edge://flags/`, find `#enable-webrtc-hide-local-ips-with-mdns`, and disable it.

Open:
- http://raspberrypi.local:9999

Documentation is available at:
- http://raspberrypi.local:8080

> Expected result: the AMP UI opens from another machine on the network and the documentation endpoint is also reachable.

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

- a prepared Raspberry Pi 5 host with the required packages
- working SSH access from your development machine
- a working Dev Container on the Pi
- a successful build
- at least one AMP pipeline started from `amp-menu`
- the AMP web UI reachable at `http://raspberrypi.local:9999`

Success looks like this: VS Code connects to the Pi, the container opens, the project builds, the pipeline starts, and the web UI is reachable from your browser.
